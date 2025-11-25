#include "config.hpp"

#define LOG_TAG "[" PLUGIN_NAME "][plugin]"
#include "log.hpp"

#include "dock.hpp"
#include "state.hpp"
#include "server.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

OBS_DECLARE_MODULE();
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return PLUGIN_NAME;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Smart Lower Thirds Plugin v" PLUGIN_VERSION;
}

static void slt_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		smart_lt::handle_scene_changed();
		if (auto *dock = LowerThird_get_dock())
			dock->updateFromState();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	LOGI("Plugin loaded (version %s)", PLUGIN_VERSION);

	smart_lt::init_state_from_disk();
	LowerThird_create_dock();
	obs_frontend_add_event_callback(slt_frontend_event, nullptr);

	return true;
}

void obs_module_unload(void)
{
	LOGI("Plugin unloading...");

	LowerThird_destroy_dock();

	LOGI("Plugin unloaded");
}
