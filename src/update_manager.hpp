// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "update_checker.hpp"
#include "update_logic.hpp"

#include <QObject>
#include <QTimer>

#include <string>

/* The update check of the plugin: it only checks and tells; it never
 * downloads or installs anything (the user opens the release page).
 *
 * Automatic check (SettingsStore updates.auto_check, on by default): first
 * looked at 60 s after OBS has finished loading, then every hour; it runs only
 * when should_auto_check() allows it (once per 24 h, failures included, never
 * while streaming or recording). Its result never opens a window: one line in
 * the OBS log and a marker (multiview context menu entry, Tools menu text)
 * when a newer, not skipped version exists. The marker is also restored at
 * startup from the last version a check reported, without any request.
 *
 * Manual check: "Check for updates now" in the settings dialog; the result is
 * shown there.
 *
 * UI thread only. Created on first use, destroyed by shutdown() (OBS exit and
 * plugin unload), which aborts a request in flight: no callback runs after it. */
class UpdateManager : public QObject {
	Q_OBJECT

public:
	static UpdateManager &instance();
	/* nullptr before the first instance() and after shutdown(). */
	static UpdateManager *existing();
	static void shutdown();

	/* OBS has finished loading: restores the marker, then schedules the
	 * automatic check (first look after 60 s, then hourly). */
	void start_schedule();

	/* Runs the automatic check if it is due; true when one was started. */
	bool evaluate_auto_check();

	/* Manual check ("Check for updates now"); ignored while one runs. */
	void check_now(bool include_prereleases);

	const UpdateState &state() const { return m_state; }
	bool busy() const { return m_checker.busy(); }

	/* Tag of the version the marker announces, "" for no marker. */
	const std::string &marker_version() const { return m_marker; }

	/* "Skip this version": no marker for the newest version found. */
	void skip_latest();

signals:
	void state_changed();
	void marker_changed();

private:
	UpdateManager();
	~UpdateManager() override;

	void start_check(bool automatic, bool include_prereleases);
	void on_result(const FetchResult &result, bool automatic, bool include_prereleases);
	void refresh_marker();

	UpdateChecker m_checker;
	QTimer m_first_timer;  /* 60 s after loading */
	QTimer m_hourly_timer; /* then every hour */
	UpdateState m_state;
	std::string m_marker;
};
