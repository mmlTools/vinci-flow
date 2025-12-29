#define LOG_TAG "[" PLUGIN_NAME "][main]"
#include "core.hpp"
#include "dock.hpp"
#include "websocket_bridge.hpp"

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

bool obs_module_load(void)
{
	LOGI("Plugin loaded (version %s)", PLUGIN_VERSION);

	smart_lt::init_from_disk();
	LowerThird_create_dock();
	return true;
}

void obs_module_post_load(void)
{
	smart_lt::ws::init();
}

void obs_module_unload(void)
{
	LOGI("Unloading plugin %s", PLUGIN_NAME);

	smart_lt::ws::shutdown();
	LowerThird_destroy_dock();

	LOGI("Plugin %s unloaded", PLUGIN_NAME);
}
