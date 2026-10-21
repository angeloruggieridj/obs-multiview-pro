// SPDX-License-Identifier: GPL-2.0-or-later
#include "update_manager.hpp"

#include "i18n.hpp"
#include "settings_store.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>

#include <cstdint>
#include <string>

static UpdateManager *g_updates = nullptr; /* UI thread only */

static constexpr int kFirstCheckDelayMs = 60 * 1000;
static constexpr int kRecheckIntervalMs = 60 * 60 * 1000;

UpdateManager &UpdateManager::instance()
{
	if (!g_updates)
		g_updates = new UpdateManager();
	return *g_updates;
}

UpdateManager *UpdateManager::existing()
{
	return g_updates;
}

void UpdateManager::shutdown()
{
	delete g_updates; /* the checker aborts a request in flight */
	g_updates = nullptr;
}

UpdateManager::UpdateManager()
{
	m_first_timer.setSingleShot(true);
	m_first_timer.setInterval(kFirstCheckDelayMs);
	m_hourly_timer.setInterval(kRecheckIntervalMs);
	connect(&m_first_timer, &QTimer::timeout, this, [this] {
		evaluate_auto_check();
		m_hourly_timer.start();
	});
	connect(&m_hourly_timer, &QTimer::timeout, this, [this] { evaluate_auto_check(); });
}

UpdateManager::~UpdateManager()
{
	m_first_timer.stop();
	m_hourly_timer.stop();
	m_checker.cancel();
}

void UpdateManager::start_schedule()
{
	refresh_marker();
	if (!m_first_timer.isActive() && !m_hourly_timer.isActive())
		m_first_timer.start();
}

bool UpdateManager::evaluate_auto_check()
{
	SettingsStore &store = SettingsStore::instance();
	UpdateSettings u = store.updates();
	const int64_t now = QDateTime::currentSecsSinceEpoch();
	const bool on_air = obs_frontend_streaming_active() || obs_frontend_recording_active();
	if (m_checker.busy() || !should_auto_check(now, u.last_check_unix, u.auto_check, on_air))
		return false;
	/* Stored before the request: a failure also waits a day. */
	u.last_check_unix = now;
	store.set_updates(u);
	obs_log(LOG_INFO, "automatic update check (once a day; one request to api.github.com)");
	start_check(true, u.include_prereleases);
	return true;
}

void UpdateManager::check_now(bool include_prereleases)
{
	if (m_checker.busy())
		return;
	SettingsStore &store = SettingsStore::instance();
	UpdateSettings u = store.updates();
	u.last_check_unix = QDateTime::currentSecsSinceEpoch();
	store.set_updates(u);
	start_check(false, include_prereleases);
}

void UpdateManager::start_check(bool automatic, bool include_prereleases)
{
	const bool started = m_checker.start([this, automatic, include_prereleases](const FetchResult &r) {
		on_result(r, automatic, include_prereleases);
	});
	if (!started)
		return;
	m_state = UpdateState();
	m_state.status = UpdateStatus::Checking;
	emit state_changed();
}

void UpdateManager::on_result(const FetchResult &result, bool automatic, bool include_prereleases)
{
	switch (result.kind) {
	case FetchResult::Kind::Ok:
		m_state = evaluate_releases(result.releases, PLUGIN_VERSION, include_prereleases,
					    {T("Update.Changelog.NoNotes").toStdString(),
					     T("Update.Changelog.Truncated").toStdString()});
		break;
	case FetchResult::Kind::NoReleases:
		m_state = UpdateState();
		m_state.status = UpdateStatus::NoReleases;
		break;
	case FetchResult::Kind::Error:
		m_state = UpdateState();
		m_state.status = UpdateStatus::Error;
		m_state.error = result.error;
		m_state.http_status = result.http_status;
		break;
	}

	SettingsStore &store = SettingsStore::instance();
	UpdateSettings u = store.updates();
	switch (m_state.status) {
	case UpdateStatus::UpdateAvailable:
		if (u.last_seen_version != m_state.latest) {
			/* Once per version, not on every check. */
			obs_log(LOG_INFO, "update available: version %s (installed %s), see Tools > Multiview Settings... > Updates",
				version_for_display(m_state.latest).c_str(), PLUGIN_VERSION);
			u.last_seen_version = m_state.latest;
		}
		break;
	case UpdateStatus::UpToDate:
	case UpdateStatus::NoReleases:
		u.last_seen_version.clear();
		break;
	case UpdateStatus::Error:
		if (automatic)
			obs_log(LOG_INFO, "automatic update check failed: %s (HTTP status %d)",
				update_error_key(m_state.error), m_state.http_status);
		break;
	case UpdateStatus::Idle:
	case UpdateStatus::Checking:
		break;
	}
	store.set_updates(u);
	emit state_changed();
	refresh_marker();
}

void UpdateManager::skip_latest()
{
	std::string version = m_state.status == UpdateStatus::UpdateAvailable ? m_state.latest : m_marker;
	if (version.empty())
		return;
	SettingsStore &store = SettingsStore::instance();
	UpdateSettings u = store.updates();
	u.skipped_version = version;
	store.set_updates(u);
	obs_log(LOG_INFO, "update %s skipped by the user", version_for_display(version).c_str());
	emit state_changed();
	refresh_marker();
}

void UpdateManager::refresh_marker()
{
	const UpdateSettings u = SettingsStore::instance().updates();
	std::string marker;
	SemVersion seen;
	SemVersion installed;
	if (!u.last_seen_version.empty() && u.last_seen_version != u.skipped_version &&
	    parse_version(u.last_seen_version, seen) && parse_version(PLUGIN_VERSION, installed) &&
	    compare_versions(seen, installed) > 0)
		marker = u.last_seen_version;
	if (marker != m_marker) {
		m_marker = marker;
		emit marker_changed();
	}
}
