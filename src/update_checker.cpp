// SPDX-License-Identifier: GPL-2.0-or-later
#include "update_checker.hpp"

/* First: on Windows it brings winsock2.h in before anything includes windows.h. */
#include <curl/curl.h>

#include <obs-module.h>
#include <plugin-support.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <cstdlib>
#include <string>
#include <utility>

namespace {

constexpr const char *kApiUrl = "https://api.github.com/repos/angeloruggieridj/obs-multiview-pro/releases?per_page=10";
constexpr size_t kMaxResponseBytes = 1024 * 1024;
constexpr long kConnectTimeoutS = 10;
constexpr long kLowSpeedTimeS = 10; /* below 1 byte/s for this long: aborted */
constexpr long kTotalTimeoutS = 15;
constexpr int kMaxRedirects = 3;
constexpr int kPumpIntervalMs = 50;
constexpr int kMaxReleases = 30;
constexpr int kMaxNotesChars = 64 * 1024;
constexpr int kMaxFieldChars = 2048;

constexpr const char *kNetworkMessage = "Could not reach GitHub. Check the internet connection.";
constexpr const char *kTimeoutMessage = "GitHub did not answer in time. Try again later.";
constexpr const char *kBadDataMessage = "Unexpected answer from GitHub (not a release list).";
constexpr const char *kTooLargeMessage = "Unexpected answer from GitHub (too large).";
constexpr const char *kRedirectMessage = "GitHub redirected the request to an unexpected address.";
constexpr const char *kNoTlsMessage = "Secure connections are not available in this OBS installation.";

std::string text_field(const QJsonObject &o, const char *key, int max_chars)
{
	const QJsonValue v = o.value(QLatin1String(key));
	if (!v.isString())
		return std::string();
	return v.toString().left(max_chars).toStdString();
}

bool bool_field(const QJsonObject &o, const char *key)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isBool() && v.toBool();
}

/* The API's release array; false for anything else. */
bool parse_releases(const std::string &body, std::vector<ReleaseInfo> &out)
{
	QJsonParseError err{};
	const QJsonDocument doc =
		QJsonDocument::fromJson(QByteArray(body.data(), static_cast<qsizetype>(body.size())), &err);
	if (err.error != QJsonParseError::NoError || !doc.isArray())
		return false;
	const QJsonArray list = doc.array();
	for (qsizetype i = 0; i < list.size(); ++i) {
		if (static_cast<int>(out.size()) >= kMaxReleases)
			break;
		const QJsonValue item = list.at(i);
		if (!item.isObject())
			continue;
		const QJsonObject o = item.toObject();
		ReleaseInfo r;
		r.tag = text_field(o, "tag_name", static_cast<int>(kMaxVersionText) + 1);
		if (r.tag.empty())
			continue;
		r.name = text_field(o, "name", 256);
		r.notes = text_field(o, "body", kMaxNotesChars);
		r.html_url = text_field(o, "html_url", kMaxFieldChars);
		r.published_at = text_field(o, "published_at", 64);
		r.prerelease = bool_field(o, "prerelease");
		r.draft = bool_field(o, "draft");
		out.push_back(std::move(r));
	}
	return true;
}

std::string curl_error_text(CURLcode code)
{
	switch (code) {
	case CURLE_OPERATION_TIMEDOUT:
		return kTimeoutMessage;
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_PEER_FAILED_VERIFICATION:
	case CURLE_SSL_CERTPROBLEM:
	case CURLE_SSL_CIPHER:
	case CURLE_SSL_CACERT_BADFILE:
	case CURLE_SSL_ISSUER_ERROR:
	case CURLE_SSL_CRL_BADFILE:
		return "Secure connection to GitHub failed.";
	case CURLE_FILESIZE_EXCEEDED:
		return kTooLargeMessage;
	case CURLE_UNSUPPORTED_PROTOCOL:
	case CURLE_URL_MALFORMAT:
		return "The update address is not valid.";
	default:
		return kNetworkMessage;
	}
}

std::string request_url()
{
	/* Test aid only (see the header): a local server instead of GitHub. */
	const std::string over = update_url_override(std::getenv("OBS_MULTIVIEW_PRO_UPDATE_URL"));
	return over.empty() ? std::string(kApiUrl) : over;
}

bool starts_with(const std::string &s, const char *prefix)
{
	const std::string p(prefix);
	return s.compare(0, p.size(), p) == 0;
}

const curl_version_info_data *curl_info()
{
	return curl_version_info(CURLVERSION_NOW);
}

bool curl_has_tls()
{
	const curl_version_info_data *v = curl_info();
	return v && (v->features & CURL_VERSION_SSL) != 0;
}

bool curl_has_async_dns()
{
	const curl_version_info_data *v = curl_info();
	return v && (v->features & CURL_VERSION_ASYNCHDNS) != 0;
}

} // namespace

std::string UpdateChecker::transport_description()
{
	const curl_version_info_data *v = curl_info();
	if (!v)
		return "libcurl (no version information)";
	std::string s = std::string("libcurl ") + (v->version ? v->version : "?");
	s += curl_has_tls() ? std::string(", TLS: ") + (v->ssl_version ? v->ssl_version : "?")
			    : std::string(", no TLS support");
	s += curl_has_async_dns() ? ", asynchronous DNS" : ", no asynchronous DNS";
	return s;
}

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
	m_pump.setInterval(kPumpIntervalMs);
	connect(&m_pump, &QTimer::timeout, this, [this] { pump(); });
}

UpdateChecker::~UpdateChecker()
{
	cancel();
	if (m_multi) {
		curl_multi_cleanup(m_multi);
		m_multi = nullptr;
	}
	if (m_global_init)
		curl_global_cleanup();
}

size_t UpdateChecker::write_cb(char *data, size_t size, size_t count, void *self)
{
	auto *c = static_cast<UpdateChecker *>(self);
	const size_t n = size * count;
	if (c->m_body.size() + n > kMaxResponseBytes) {
		c->m_too_large = true;
		return 0; /* aborts the transfer (CURLE_WRITE_ERROR) */
	}
	c->m_body.append(data, n);
	return n;
}

bool UpdateChecker::start(Callback done)
{
	if (busy())
		return false;
	m_done = std::move(done);
	m_redirects = 0;

	if (!m_global_init) {
		if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
			fail_soon("The update check could not start.");
			return true;
		}
		m_global_init = true;
		obs_log(LOG_INFO, "update check transport: %s", transport_description().c_str());
	}

	const std::string url = request_url();
	m_plain_http_allowed = starts_with(url, "http://"); /* only a validated local override */
	std::string error;
	if (!begin_request(url, error))
		fail_soon(error);
	return true;
}

bool UpdateChecker::begin_request(const std::string &url, std::string &error)
{
	const QUrl parsed(QString::fromStdString(url), QUrl::StrictMode);
	const bool https = parsed.scheme() == QLatin1String("https");
	const bool http = parsed.scheme() == QLatin1String("http") && m_plain_http_allowed;
	if (!parsed.isValid() || (!https && !http) || !parsed.userInfo().isEmpty()) {
		error = "The update address is not valid.";
		return false;
	}
	if (https && !curl_has_tls()) {
		error = kNoTlsMessage;
		return false;
	}
	if (!curl_has_async_dns()) {
		/* Name resolution would block OBS's UI thread. */
		error = "The update check is not available in this OBS installation.";
		return false;
	}
	if (!m_multi) {
		m_multi = curl_multi_init();
		if (!m_multi) {
			error = "The update check could not start.";
			return false;
		}
	}
	CURL *e = curl_easy_init();
	if (!e) {
		error = "The update check could not start.";
		return false;
	}

	curl_slist *h = nullptr;
	h = curl_slist_append(h, "Accept: application/vnd.github+json");
	h = curl_slist_append(h, "X-GitHub-Api-Version: 2022-11-28");
	h = curl_slist_append(h, "Accept-Language: en");
	const std::string agent = std::string("obs-multiview-pro/") + PLUGIN_VERSION;

	curl_easy_setopt(e, CURLOPT_URL, url.c_str());
	/* Only the scheme checked above; failure on an old libcurl is harmless
	 * because the scheme was checked here and redirects are not followed
	 * by libcurl. */
	curl_easy_setopt(e, CURLOPT_PROTOCOLS_STR, http ? "http,https" : "https");
	curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 0L); /* followed by hand, see on_done */
	curl_easy_setopt(e, CURLOPT_HTTPHEADER, h);
	curl_easy_setopt(e, CURLOPT_USERAGENT, agent.c_str());
	curl_easy_setopt(e, CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutS);
	curl_easy_setopt(e, CURLOPT_TIMEOUT, kTotalTimeoutS);
	curl_easy_setopt(e, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(e, CURLOPT_LOW_SPEED_TIME, kLowSpeedTimeS);
	curl_easy_setopt(e, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(kMaxResponseBytes));
	curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &UpdateChecker::write_cb);
	curl_easy_setopt(e, CURLOPT_WRITEDATA, static_cast<void *>(this));
	/* Certificates are always verified (these are the defaults, set
	 * explicitly). Schannel: a revocation server that cannot be reached
	 * does not fail the check; a revoked certificate still does. */
	curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(e, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
	/* Teardown never waits for a name lookup in progress. */
	curl_easy_setopt(e, CURLOPT_QUICK_EXIT, 1L);
	/* No cookies (the cookie engine is off unless enabled), no .netrc, no
	 * proxy credentials of ours. */
	curl_easy_setopt(e, CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));

	if (curl_multi_add_handle(m_multi, e) != CURLM_OK) {
		curl_slist_free_all(h);
		curl_easy_cleanup(e);
		error = "The update check could not start.";
		return false;
	}
	m_easy = e;
	m_headers = h;
	m_body.clear();
	m_too_large = false;
	m_pump.start();
	return true;
}

void UpdateChecker::release_transfer()
{
	m_pump.stop();
	if (m_easy) {
		CURL *e = m_easy;
		m_easy = nullptr;
		if (m_multi)
			curl_multi_remove_handle(m_multi, e);
		curl_easy_cleanup(e);
	}
	if (m_headers) {
		curl_slist_free_all(static_cast<curl_slist *>(m_headers));
		m_headers = nullptr;
	}
}

void UpdateChecker::cancel()
{
	m_pending_error = false;
	m_done = nullptr;
	release_transfer();
}

void UpdateChecker::pump()
{
	if (!m_easy || !m_multi)
		return;
	int running = 0;
	curl_multi_perform(m_multi, &running); /* never blocks */
	int left = 0;
	while (CURLMsg *msg = curl_multi_info_read(m_multi, &left)) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == m_easy) {
			on_done(static_cast<int>(msg->data.result));
			return; /* the handle is gone; a redirect added a new one */
		}
	}
}

void UpdateChecker::on_done(int curl_code)
{
	const CURLcode code = static_cast<CURLcode>(curl_code);
	long http = 0;
	curl_easy_getinfo(m_easy, CURLINFO_RESPONSE_CODE, &http);
	const char *ctype = nullptr;
	curl_easy_getinfo(m_easy, CURLINFO_CONTENT_TYPE, &ctype);
	const std::string type = ctype ? QString::fromUtf8(ctype).toLower().toStdString() : std::string();
	const char *location = nullptr;
	curl_easy_getinfo(m_easy, CURLINFO_REDIRECT_URL, &location);
	const std::string redirect = location ? std::string(location) : std::string();
	const bool too_large = m_too_large;
	release_transfer();

	FetchResult r;
	r.kind = FetchResult::Kind::Error;

	if (too_large || code == CURLE_FILESIZE_EXCEEDED) {
		r.message = kTooLargeMessage;
		finish(r);
		return;
	}
	if (code != CURLE_OK) {
		r.message = curl_error_text(code);
		finish(r);
		return;
	}
	if (http >= 300 && http <= 399) {
		const QUrl target(QString::fromStdString(redirect), QUrl::StrictMode);
		const bool ok = !redirect.empty() && target.isValid() && target.userInfo().isEmpty() &&
				target.port() == -1 &&
				is_allowed_redirect(target.scheme().toStdString(), target.host().toLower().toStdString());
		std::string error;
		if (!ok || m_redirects >= kMaxRedirects) {
			r.message = kRedirectMessage;
		} else {
			++m_redirects;
			m_plain_http_allowed = false; /* a redirect is always https */
			if (begin_request(redirect, error))
				return;
			r.message = error;
		}
		finish(r);
		return;
	}
	if (http == 404) {
		r.kind = FetchResult::Kind::NoReleases;
		finish(r);
		return;
	}
	if (http != 200) {
		r.message = http == 0 ? std::string(kNetworkMessage) : http_error_text(static_cast<int>(http));
		finish(r);
		return;
	}
	if (!starts_with(type, "application/json") && !starts_with(type, "application/vnd.github")) {
		r.message = "Unexpected answer from GitHub (not JSON).";
		finish(r);
		return;
	}
	std::vector<ReleaseInfo> releases;
	const bool parsed = parse_releases(m_body, releases);
	m_body.clear();
	if (!parsed) {
		r.message = kBadDataMessage;
		finish(r);
		return;
	}
	r.kind = FetchResult::Kind::Ok;
	r.releases = std::move(releases);
	finish(r);
}

void UpdateChecker::fail_soon(const std::string &message)
{
	m_pending_error = true;
	QTimer::singleShot(0, this, [this, message] {
		if (!m_pending_error)
			return; /* cancelled meanwhile */
		m_pending_error = false;
		FetchResult r;
		r.kind = FetchResult::Kind::Error;
		r.message = message;
		finish(r);
	});
}

void UpdateChecker::finish(const FetchResult &result)
{
	Callback done = std::move(m_done);
	m_done = nullptr;
	if (done)
		done(result);
}
