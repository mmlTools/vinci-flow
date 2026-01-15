#define LOG_TAG "[" PLUGIN_NAME "][main]"
#include "core.hpp"
#include "dock.hpp"
#include "headers/api.hpp"
#include "websocket_bridge.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

#include <QTimer>
#include <QMetaObject>
#include <QUrl>

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

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING &&
	    event != OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED &&
	    event != OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED)
		return;

	auto *dock = LowerThird_get_dock();
	if (!dock)
		return;

	QMetaObject::invokeMethod(dock, [dock]() { dock->refreshBrowserSources(); }, Qt::QueuedConnection);
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
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	smart_lt::ws::init();

	// Update check: compare local PLUGIN_VERSION vs API "plugin_version".
	// IMPORTANT: Connect BEFORE init(), because init() may synchronously emit
	// lowerThirdsUpdated() when loading the on-disk cache.
	if (auto *dock = LowerThird_get_dock()) {
		auto &api = smart_lt::api::ApiClient::instance();
		QObject::connect(&api, &smart_lt::api::ApiClient::lowerThirdsUpdated, dock, [dock]() {
			auto &api2 = smart_lt::api::ApiClient::instance();
			const QString remoteV = api2.remotePluginVersion().trimmed();
			const QString localV = QStringLiteral(PLUGIN_VERSION);
			QMetaObject::invokeMethod(dock,
					      [dock, remoteV, localV]() { dock->setUpdateAvailable(remoteV, localV); },
					      Qt::QueuedConnection);
		}, Qt::UniqueConnection);
	}

	// Marketplace preload (cached on disk; refresh is async).
	smart_lt::api::ApiClient::instance().init();

	// Also apply the version check once immediately after init() (covers the case
	// where cache load happened before the Qt signal loop processes queued calls).
	if (auto *dock = LowerThird_get_dock()) {
		QTimer::singleShot(0, dock, [dock]() {
			auto &api = smart_lt::api::ApiClient::instance();
			dock->setUpdateAvailable(api.remotePluginVersion().trimmed(), QStringLiteral(PLUGIN_VERSION));
		});
	}
	if (auto *dock = LowerThird_get_dock()) {
		QTimer::singleShot(250, dock, [dock]() { dock->refreshBrowserSources(); });
		QTimer::singleShot(1000, dock, [dock]() { dock->refreshBrowserSources(); });
	}
}

void obs_module_unload(void)
{
	LOGI("Unloading plugin %s", PLUGIN_NAME);

	smart_lt::ws::shutdown();
	LowerThird_destroy_dock();

	LOGI("Plugin %s unloaded", PLUGIN_NAME);
}
