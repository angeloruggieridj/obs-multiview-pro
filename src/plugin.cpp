#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAction>

#include "multiview_window.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void open_multiview_window()
{
	/* Parentless top-level window: it can live on any screen. Deleted on close. */
	auto *window = new MultiviewWindow(nullptr);
	window->show();
}

void register_menu_action()
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Multiview"));
	QObject::connect(action, &QAction::triggered, [] { open_multiview_window(); });
	obs_log(LOG_INFO, "menu action 'Tools > Multiview' registered");
}

bool obs_module_load(void)
{
	register_menu_action();
	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
