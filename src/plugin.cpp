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
#include "settings_store.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* All state below is touched on the Qt UI thread only (load, menu trigger,
 * frontend events and unload all run there). */
static QPointer<QAction> g_action;
static QMetaObject::Connection g_connection;
static std::vector<QPointer<MultiviewWindow>> g_windows;

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
	if (event == OBS_FRONTEND_EVENT_EXIT)
		close_all_windows();
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
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	/* The action itself belongs to OBS's menu; just make sure nothing in this
	 * DLL stays connected to it. */
	QObject::disconnect(g_connection);
	g_connection = QMetaObject::Connection();
	g_action.clear();
	close_all_windows();
	SettingsStore::shutdown(); /* after the windows: their final saves are flushed */
	obs_log(LOG_INFO, "plugin unloaded");
}
