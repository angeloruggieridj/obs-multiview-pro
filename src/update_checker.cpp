// SPDX-License-Identifier: GPL-2.0-or-later
#include "update_checker.hpp"

#include <plugin-support.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <QVariant>
#if QT_CONFIG(ssl)
#include <QSslSocket>
#endif

#include <cstdlib>
#include <utility>

namespace {

constexpr const char *kApiUrl = "https://api.github.com/repos/angeloruggieridj/obs-multiview-pro/releases?per_page=10";
constexpr qint64 kMaxResponseBytes = 1024 * 1024;
constexpr int kIdleTimeoutMs = 10000;     /* no data for this long: aborted */
constexpr int kTransferTimeoutMs = 12000; /* the same in Qt, as a backstop */
constexpr int kOverallTimeoutMs = 15000;  /* a server that trickles data */
constexpr int kMaxRedirects = 3;
constexpr int kMaxReleases = 30;
constexpr int kMaxNotesChars = 64 * 1024;
constexpr int kMaxFieldChars = 2048;

constexpr const char *kNetworkMessage = "Could not reach GitHub. Check the internet connection.";
constexpr const char *kTimeoutMessage = "GitHub did not answer in time. Try again later.";
constexpr const char *kBadDataMessage = "Unexpected answer from GitHub (not a release list).";

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
bool parse_releases(const QByteArray &body, std::vector<ReleaseInfo> &out)
{
	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
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

std::string network_error_text(QNetworkReply::NetworkError error)
{
	switch (error) {
	case QNetworkReply::TimeoutError:
	case QNetworkReply::OperationCanceledError: /* the transfer timeout aborts like this */
		return kTimeoutMessage;
	case QNetworkReply::SslHandshakeFailedError:
		return "Secure connection to GitHub failed.";
	case QNetworkReply::TooManyRedirectsError:
	case QNetworkReply::InsecureRedirectError:
		return "GitHub redirected the request to an unexpected address.";
	default:
		return kNetworkMessage;
	}
}

QString request_url()
{
	/* Test aid only (see the header): a local server instead of GitHub. */
	const std::string over = update_url_override(std::getenv("OBS_MULTIVIEW_PRO_UPDATE_URL"));
	return QString::fromStdString(over.empty() ? std::string(kApiUrl) : over);
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
	auto time_out = [this] {
		if (m_reply) {
			m_abort_reason = AbortReason::Timeout;
			m_reply->abort(); /* finished() follows synchronously */
		}
	};
	m_deadline.setSingleShot(true);
	m_deadline.setInterval(kOverallTimeoutMs);
	connect(&m_deadline, &QTimer::timeout, this, time_out);
	m_idle.setSingleShot(true);
	m_idle.setInterval(kIdleTimeoutMs);
	connect(&m_idle, &QTimer::timeout, this, time_out);
}

UpdateChecker::~UpdateChecker()
{
	cancel();
}

bool UpdateChecker::start(Callback done)
{
	if (busy())
		return false;
	m_done = std::move(done);
	m_body.clear();
	m_abort_reason = AbortReason::None;

	const QUrl url(request_url(), QUrl::StrictMode);
	if (!url.isValid()) {
		fail_soon("The update address is not valid.");
		return true;
	}
	if (url.scheme() == QLatin1String("https")) {
#if QT_CONFIG(ssl)
		const bool tls = QSslSocket::supportsSsl();
#else
		const bool tls = false;
#endif
		if (!tls) {
			fail_soon("Secure connections are not available in this OBS installation.");
			return true;
		}
	}

	if (!m_nam)
		m_nam = new QNetworkAccessManager(this);

	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("obs-multiview-pro/%1").arg(QString::fromUtf8(PLUGIN_VERSION)));
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
	/* Fixed, so that Qt does not send the system language. */
	req.setRawHeader("Accept-Language", "en");
	req.setTransferTimeout(kTransferTimeoutMs);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
	req.setMaximumRedirectsAllowed(kMaxRedirects);
	req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
	req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
	req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

	m_reply = m_nam->get(req);
	QNetworkReply *reply = m_reply;

	connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &target) {
		const bool ok = target.isValid() && target.userInfo().isEmpty() && target.port() == -1 &&
				is_allowed_redirect(target.scheme().toStdString(), target.host().toLower().toStdString());
		if (ok) {
			emit reply->redirectAllowed();
		} else {
			m_abort_reason = AbortReason::BadRedirect;
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
		m_idle.start();
		const QVariant len = reply->header(QNetworkRequest::ContentLengthHeader);
		if (len.isValid() && len.toLongLong() > kMaxResponseBytes) {
			m_abort_reason = AbortReason::TooLarge;
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
		if (m_abort_reason != AbortReason::None)
			return;
		m_idle.start();
		m_body += reply->read(kMaxResponseBytes + 1 - m_body.size());
		if (m_body.size() > kMaxResponseBytes) {
			m_abort_reason = AbortReason::TooLarge;
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this] { on_finished(); });
	m_deadline.start();
	m_idle.start();
	return true;
}

void UpdateChecker::cancel()
{
	m_deadline.stop();
	m_idle.stop();
	m_pending_error = false;
	m_done = nullptr;
	if (QNetworkReply *reply = m_reply) {
		m_reply = nullptr;
		disconnect(reply, nullptr, this, nullptr); /* abort() must not call back */
		reply->abort();
		delete reply;
	}
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

void UpdateChecker::on_finished()
{
	QNetworkReply *reply = m_reply;
	if (!reply)
		return;
	m_reply = nullptr;
	m_deadline.stop();
	m_idle.stop();
	disconnect(reply, nullptr, this, nullptr);
	reply->deleteLater();

	FetchResult r;
	r.kind = FetchResult::Kind::Error;
	const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	switch (m_abort_reason) {
	case AbortReason::TooLarge:
		r.message = "Unexpected answer from GitHub (too large).";
		finish(r);
		return;
	case AbortReason::BadRedirect:
		r.message = "GitHub redirected the request to an unexpected address.";
		finish(r);
		return;
	case AbortReason::Timeout:
		r.message = kTimeoutMessage;
		finish(r);
		return;
	case AbortReason::None:
		break;
	}

	if (http == 404) {
		r.kind = FetchResult::Kind::NoReleases;
		finish(r);
		return;
	}
	if (http != 0 && http != 200) {
		r.message = http_error_text(http);
		finish(r);
		return;
	}
	if (reply->error() != QNetworkReply::NoError || http == 0) {
		r.message = network_error_text(reply->error());
		finish(r);
		return;
	}

	const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
	if (!type.startsWith(QLatin1String("application/json")) &&
	    !type.startsWith(QLatin1String("application/vnd.github"))) {
		r.message = "Unexpected answer from GitHub (not JSON).";
		finish(r);
		return;
	}
	m_body += reply->read(kMaxResponseBytes + 1 - m_body.size());
	if (m_body.size() > kMaxResponseBytes) {
		r.message = "Unexpected answer from GitHub (too large).";
		finish(r);
		return;
	}
	std::vector<ReleaseInfo> releases;
	if (!parse_releases(m_body, releases)) {
		r.message = kBadDataMessage;
		finish(r);
		return;
	}
	m_body.clear();
	r.kind = FetchResult::Kind::Ok;
	r.releases = std::move(releases);
	finish(r);
}
