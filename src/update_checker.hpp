// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "update_logic.hpp"

#include <QObject>
#include <QTimer>

#include <functional>
#include <string>
#include <vector>

/* What one request to the GitHub releases API gave. */
struct FetchResult {
	enum class Kind {
		Ok,         /* `releases` holds the list (it may be empty) */
		NoReleases, /* HTTP 404: nothing published (or no such repository) */
		Error,      /* `message` says what went wrong, in a few words */
	};
	Kind kind = Kind::Error;
	std::vector<ReleaseInfo> releases;
	std::string message;
};

/* One HTTPS GET of
 *   https://api.github.com/repos/angeloruggieridj/obs-multiview-pro/releases?per_page=10
 * through the libcurl that OBS itself ships (OBS's Qt has no TLS backend on
 * Windows). Headers: Accept, X-GitHub-Api-Version (what GitHub asks for), a
 * fixed "Accept-Language: en" and User-Agent "obs-multiview-pro/<version>": no
 * cookies, no credentials, nothing else about the machine. Certificates are
 * always verified (system store / the TLS library's default CA bundle);
 * revocation lookups are off because Schannel would do them synchronously on
 * the UI thread (see update_checker.cpp).
 *
 * Never blocks the UI thread: the transfer runs through curl_multi, pumped by
 * a 50 ms QTimer on the UI thread while a request is in flight (DNS is
 * resolved asynchronously by libcurl; a libcurl without asynchronous DNS is
 * not used). No worker threads of our own.
 *
 * Limits: 10 s to connect, aborted below 1 byte/s for 10 s, 15 s in total,
 * 1 MB of response, JSON only, redirects followed by hand (at most 3) and only
 * to https://api.github.com or https://github.com. No retries. One
 * request at a time: start() while one is in flight is ignored. Destroying the
 * checker (or cancel()) removes and frees the transfer at once; the callback
 * is never called afterwards.
 *
 * Test aid: the environment variable OBS_MULTIVIEW_PRO_UPDATE_URL replaces the
 * address only when it points at a loopback host (127.0.0.1, localhost,
 * [::1]; http or https; see update_url_override), so the check can be run
 * against a local test server; plain http is allowed only for such an
 * override. Any other value is ignored. */
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	using Callback = std::function<void(const FetchResult &)>;

	explicit UpdateChecker(QObject *parent = nullptr);
	~UpdateChecker() override;

	bool busy() const { return m_easy != nullptr || m_pending_error; }

	/* Starts a request; `done` is called once on the UI thread, never from
	 * inside start(). False (and `done` dropped) when one is in flight. */
	bool start(Callback done);

	/* Aborts a request in flight without calling its callback. */
	void cancel();

	/* "libcurl 8.12.1, Schannel" or why secure requests are not possible;
	 * for the log. */
	static std::string transport_description();

private:
	static size_t write_cb(char *data, size_t size, size_t count, void *self);

	bool begin_request(const std::string &url, std::string &error);
	void release_transfer();
	void pump();
	void on_done(int curl_code);
	void fail_soon(const std::string &message);
	void finish(const FetchResult &result);

	void *m_multi = nullptr;  /* CURLM*, created on first use */
	void *m_easy = nullptr;   /* CURL* in flight */
	void *m_headers = nullptr; /* curl_slist* of m_easy */
	bool m_plain_http_allowed = false; /* only for a local test override */
	int m_redirects = 0;
	bool m_too_large = false;
	std::string m_body;
	Callback m_done;
	bool m_pending_error = false; /* an error is being reported without a request */
	QTimer m_pump;
};
