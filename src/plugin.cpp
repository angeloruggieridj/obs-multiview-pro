// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAction>
#include <QPointer>
#include <QObject>

#include <algorithm>
#include <utility>
#include <vector>

#include "multiview_window.hpp"
#include "settings_dialog.hpp"
#include "settings_store.hpp"
#include "update_logic.hpp"
#include "update_manager.hpp"

OBS_DECLARE_MODULE()

/* All state below is touched on the Qt UI thread only (load, menu trigger,
 * frontend events and unload all run there). */
static QPointer<QAction> g_action;
static QMetaObject::Connection g_connection;
static QPointer<QAction> g_settings_action;
static QMetaObject::Connection g_settings_connection;
static std::vector<QPointer<MultiviewWindow>> g_windows;
static QMetaObject::Connection g_marker_connection;
/* Set once EXIT was handled: OBS has cleared the frontend callbacks by then. */
static bool g_exit_handled = false;

static const char *kSettingsText = "Multiview Settings...";
static const char *kSettingsUpdateText = "Multiview Settings... (update available)";

/* The Tools menu entry tells about an update found by a check. */
static void update_settings_action_text()
{
	if (!g_settings_action)
		return;
	const UpdateManager *m = UpdateManager::existing();
	const bool marker = m && !m->marker_version().empty();
	g_settings_action->setText(QString::fromUtf8(marker ? kSettingsUpdateText : kSettingsText));
}

static void start_update_checks()
{
	UpdateManager &m = UpdateManager::instance();
	QObject::disconnect(g_marker_connection);
	g_marker_connection = QObject::connect(&m, &UpdateManager::marker_changed, &m, [] { update_settings_action_text(); });
	m.start_schedule();
	update_settings_action_text();
}

static void stop_update_checks()
{
	QObject::disconnect(g_marker_connection);
	g_marker_connection = QMetaObject::Connection();
	UpdateManager::shutdown(); /* aborts a request in flight */
}

static void close_all_windows()
{
	auto windows = std::move(g_windows);
	g_windows.clear();
	for (auto &w : windows) {
		if (w)
			delete w.data();
	}
}

/* Lowest instance index not used by an open window. Settings are saved under
 * this index; a closed window frees its index and the next window to take it
 * finds those settings again. */
static int lowest_free_instance()
{
	for (int index = 0;; ++index) {
		bool used = false;
		for (const auto &w : g_windows)
			if (w && w->instance() == index) {
				used = true;
				break;
			}
		if (!used)
			return index;
	}
}

static void open_multiview_window()
{
	g_windows.erase(std::remove_if(g_windows.begin(), g_windows.end(),
				       [](const QPointer<MultiviewWindow> &w) { return w.isNull(); }),
			g_windows.end());
	/* Parentless top-level window: it can live on any screen. Deleted on close. */
	auto *window = new MultiviewWindow(lowest_free_instance(), nullptr);
	g_windows.emplace_back(window);
	window->show();
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		start_update_checks(); /* the first automatic one waits 60 s */
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		g_exit_handled = true;
		close_settings_dialog(); /* takes any preview back before the windows go */
		stop_update_checks();
		close_all_windows();
	}
}

void register_menu_action()
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Multiview"));
	if (!action) {
		obs_log(LOG_ERROR, "could not create the 'Tools > Multiview' menu action");
		return;
	}
	g_action = action;
	g_connection = QObject::connect(action, &QAction::triggered, action, [] { open_multiview_window(); });
	obs_log(LOG_INFO, "menu action 'Tools > Multiview' registered");

	auto *settings = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(kSettingsText));
	if (!settings) {
		obs_log(LOG_ERROR, "could not create the 'Tools > Multiview Settings...' menu action");
		return;
	}
	g_settings_action = settings;
	g_settings_connection =
		QObject::connect(settings, &QAction::triggered, settings, [] {
			const UpdateManager *m = UpdateManager::existing();
			const bool marker = m && !m->marker_version().empty();
			open_settings_dialog(marker ? SettingsPage::Updates : SettingsPage::Keep);
		});
}

bool obs_module_load(void)
{
	register_menu_action();
	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (!g_exit_handled) /* after EXIT the frontend API is already gone */
		obs_frontend_remove_event_callback(frontend_event, nullptr);
	/* The action itself belongs to OBS's menu; just make sure nothing in this
	 * DLL stays connected to it. */
	QObject::disconnect(g_connection);
	g_connection = QMetaObject::Connection();
	g_action.clear();
	QObject::disconnect(g_settings_connection);
	g_settings_connection = QMetaObject::Connection();
	if (g_settings_action)
		g_settings_action->setText(QString::fromUtf8(kSettingsText));
	g_settings_action.clear();
	close_settings_dialog();
	stop_update_checks(); /* before the store: it may still save the last check time */
	close_all_windows();
	SettingsStore::shutdown(); /* after the windows: their final saves are flushed */
	obs_log(LOG_INFO, "plugin unloaded");
}
