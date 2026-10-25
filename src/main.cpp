#define LOG_TAG "[" PLUGIN_NAME "][main]"
#include "core.hpp"
#include "dock.hpp"
#include "hotkeys.hpp"
#include "websocket_bridge.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

#include <QTimer>
#include <QMetaObject>

OBS_DECLARE_MODULE();
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "VinciFlow";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "VinciFlow lower thirds controller v" PLUGIN_VERSION;
}

static void start_hotkeys(vflow::ui::LowerThirdDock *dock)
{
	vflow::hotkeys::init([dock](const QString &id) { dock->toggleLowerThird(id); },
			     [dock](const QString &id) { dock->toggleGroup(id); });
	// Let the dock pick up the hotkey hints now that bindings are loaded.
	vflow::notify_list_updated();
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	auto *dock = LowerThird_get_dock();

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (dock) {
			start_hotkeys(dock);
			dock->onSceneCollectionChanged();
		}
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (dock)
			QMetaObject::invokeMethod(
				dock, [dock]() { dock->onSceneCollectionChanged(); }, Qt::QueuedConnection);
		break;

	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		if (dock)
			QMetaObject::invokeMethod(
				dock, [dock]() { dock->refreshBrowserSources(); }, Qt::QueuedConnection);
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		// Persist hotkey bindings changed in Settings > Hotkeys, then release
		// everything that talks to the frontend while it still exists.
		if (vflow::has_output_dir())
			vflow::save_state_json();
		vflow::ws::shutdown();
		vflow::hotkeys::shutdown();
		if (dock)
			dock->stopAll();
		break;

	default:
		break;
	}
}

bool obs_module_load(void)
{
	LOGI("Plugin loaded (version %s)", PLUGIN_VERSION);

	vflow::init_from_disk();
	LowerThird_create_dock();
	return true;
}

void obs_module_post_load(void)
{
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	vflow::ws::init();

	if (auto *dock = LowerThird_get_dock())
		QTimer::singleShot(250, dock, [dock]() { dock->refreshBrowserSources(); });
}

void obs_module_unload(void)
{
	LOGI("Unloading plugin %s", PLUGIN_NAME);

	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	vflow::ws::shutdown();
	vflow::hotkeys::shutdown();
	LowerThird_destroy_dock();

	LOGI("Plugin %s unloaded", PLUGIN_NAME);
}
