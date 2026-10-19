#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAction>
#include <QPointer>
#include <QObject>

#include <algorithm>
#include <vector>

#include "multiview_window.hpp"

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

static void open_multiview_window()
{
	/* Parentless top-level window: it can live on any screen. Deleted on close. */
	auto *window = new MultiviewWindow(nullptr);
	g_windows.erase(std::remove_if(g_windows.begin(), g_windows.end(),
				       [](const QPointer<MultiviewWindow> &w) { return w.isNull(); }),
			g_windows.end());
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
	obs_log(LOG_INFO, "plugin unloaded");
}
