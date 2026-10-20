// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "update_logic.hpp"

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <functional>
#include <string>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

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
 * with only the headers GitHub asks for (Accept, X-GitHub-Api-Version), a
 * fixed "Accept-Language: en" (Qt would send the system language) and
 * User-Agent "obs-multiview-pro/<version>": no cookies, no credentials, no
 * cache, nothing else about the machine.
 *
 * Asynchronous on the UI thread (QNetworkAccessManager); nothing here ever
 * waits. Limits: 10 s without data or 15 s in total, 1 MB of response,
 * redirects only to https hosts under github.com / githubusercontent.com,
 * JSON only. No retries. One request at a time: start() while one is in
 * flight is ignored. Destroying the checker (or cancel()) aborts the request
 * and the callback is never called afterwards.
 *
 * Test aid: the environment variable OBS_MULTIVIEW_PRO_UPDATE_URL replaces the
 * address when it starts with "https://", "http://127.0.0.1:" or
 * "http://localhost:" (ignored otherwise), so the check can be run against a
 * local test server. */
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	using Callback = std::function<void(const FetchResult &)>;

	explicit UpdateChecker(QObject *parent = nullptr);
	~UpdateChecker() override;

	bool busy() const { return m_reply != nullptr || m_pending_error; }

	/* Starts a request; `done` is called once on the UI thread, never from
	 * inside start(). False (and `done` dropped) when one is in flight. */
	bool start(Callback done);

	/* Aborts a request in flight without calling its callback. */
	void cancel();

private:
	enum class AbortReason { None, TooLarge, BadRedirect, Timeout };

	void on_finished();
	void fail_soon(const std::string &message);
	void finish(const FetchResult &result);

	QNetworkAccessManager *m_nam = nullptr; /* created on first use */
	QNetworkReply *m_reply = nullptr;
	Callback m_done;
	QByteArray m_body;
	AbortReason m_abort_reason = AbortReason::None;
	bool m_pending_error = false; /* an error is being reported without a request */
	QTimer m_deadline;            /* overall time limit */
	QTimer m_idle;                /* no data for this long */
};
