#define LOG_TAG "[" PLUGIN_NAME "][ws]"
#include "websocket_bridge.hpp"

#include "core.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>

// vendored header
#include "thirdparty/obs-websocket-api.h"

#include <obs-module.h>
#include <obs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vflow::ws {

static obs_websocket_vendor g_vendor = nullptr;
static const char *kVendorName = "vinci-flow";
static uint64_t g_core_listener_token = 0;
static std::atomic_bool g_accepting{false};

// -------------------------
// Local helpers
// -------------------------
static std::string sanitize_id_local(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (unsigned char ch : s) {
		const char c = (char)ch;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
		    c == '-') {
			out.push_back(c);
		}
	}
	return out;
}

static void set_ok(obs_data_t *response, bool ok)
{
	obs_data_set_bool(response, "ok", ok);
}

static void set_error(obs_data_t *response, const char *msg)
{
	set_ok(response, false);
	obs_data_set_string(response, "error", msg ? msg : "unknown");
}

static const char *reason_to_str(vflow::list_change_reason r)
{
	using R = vflow::list_change_reason;
	switch (r) {
	case R::Create:
		return "create";
	case R::Clone:
		return "clone";
	case R::Delete:
		return "delete";
	case R::Reload:
		return "reload";
	case R::Update:
		return "update";
	default:
		return "unknown";
	}
}

// -------------------------
// CORE -> WS vendor events (single source of truth)
// -------------------------
static void on_core_event(const vflow::core_event &ev, void *user)
{
	UNUSED_PARAMETER(user);
	if (!g_vendor)
		return;

	if (ev.type == vflow::event_type::VisibilityChanged) {
		obs_data_t *data = obs_data_create();
		obs_data_set_string(data, "id", ev.id.c_str());
		obs_data_set_bool(data, "visible", ev.visible);

		obs_data_array_t *arr = obs_data_array_create();
		for (const auto &vid : ev.visible_ids) {
			obs_data_t *o = obs_data_create();
			obs_data_set_string(o, "id", vid.c_str());
			obs_data_array_push_back(arr, o);
			obs_data_release(o);
		}
		obs_data_set_array(data, "visibleIds", arr);
		obs_data_array_release(arr);

		obs_websocket_vendor_emit_event(g_vendor, "LowerThirdsVisibilityChanged", data);
		obs_data_release(data);
		return;
	}

	if (ev.type == vflow::event_type::ListChanged) {
		obs_data_t *data = obs_data_create();
		obs_data_set_string(data, "reason", reason_to_str(ev.reason));
		if (!ev.id.empty())
			obs_data_set_string(data, "id", ev.id.c_str());
		if (!ev.id2.empty())
			obs_data_set_string(data, "id2", ev.id2.c_str());
		obs_data_set_int(data, "count", (long long)ev.count);

		obs_websocket_vendor_emit_event(g_vendor, "LowerThirdsListChanged", data);
		obs_data_release(data);
		return;
	}

	if (ev.type == vflow::event_type::Reloaded) {
		obs_data_t *data = obs_data_create();
		obs_data_set_bool(data, "ok", ev.ok);
		obs_data_set_int(data, "count", (long long)ev.count);

		obs_websocket_vendor_emit_event(g_vendor, "LowerThirdsReloaded", data);
		obs_data_release(data);
		return;
	}
}

// -------------------------
// Vendor request callbacks
// -------------------------
static void req_ListLowerThirds(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	obs_data_array_t *items = obs_data_array_create();

	for (const auto &c : vflow::all_const()) {
		obs_data_t *it = obs_data_create();
		obs_data_set_string(it, "id", c.id.c_str());
		obs_data_set_string(it, "title", c.title.c_str());
		obs_data_set_string(it, "subtitle", c.subtitle.c_str());
		obs_data_set_bool(it, "isVisible", vflow::is_visible(c.id));
		obs_data_set_int(it, "repeatEverySec", c.repeat_every_sec);
		obs_data_set_int(it, "repeatVisibleSec", c.repeat_visible_sec);
		obs_data_set_string(it, "hotkey", c.hotkey.c_str());

		obs_data_set_string(it, "primaryColor", c.primary_color.c_str());
		obs_data_set_string(it, "secondaryColor", c.secondary_color.c_str());
		obs_data_set_string(it, "titleColor", c.title_color.c_str());
		obs_data_set_string(it, "subtitleColor", c.subtitle_color.c_str());

		// Legacy fields for older docks
		obs_data_set_string(it, "bgColor", c.primary_color.c_str());
		obs_data_set_string(it, "textColor", c.title_color.c_str());
		obs_data_set_int(it, "opacity", c.opacity);
		obs_data_set_int(it, "radius", c.radius);

		obs_data_array_push_back(items, it);
		obs_data_release(it);
	}

	set_ok(response, true);
	obs_data_set_array(response, "items", items);
	obs_data_array_release(items);
}

static void req_GetVisible(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &id : vflow::visible_ids()) {
		obs_data_t *o = obs_data_create();
		obs_data_set_string(o, "id", id.c_str());
		obs_data_array_push_back(arr, o);
		obs_data_release(o);
	}

	set_ok(response, true);
	obs_data_set_array(response, "visibleIds", arr);
	obs_data_array_release(arr);
}

static void req_SetVisible(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	const bool visible = obs_data_get_bool(request, "visible");

	std::string sid = sanitize_id_local(idC ? idC : "");
	if (sid.empty() || !vflow::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	if (!vflow::set_visible_persist(sid, visible)) {
		set_error(response, "Failed to set visibility");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "visible", vflow::is_visible(sid));
}

static bool parse_json_object(obs_data_t *data, QJsonObject &out)
{
	const char *json = obs_data_get_json(data);
	if (!json)
		return false;

	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject())
		return false;

	out = doc.object();
	return true;
}

static void req_SetTemplateParameters(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	QJsonObject root;
	if (!parse_json_object(request, root)) {
		set_error(response, "Invalid request payload");
		return;
	}

	const std::string sid = sanitize_id_local(root.value("id").toString().toStdString());
	if (sid.empty() || !vflow::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	const QJsonValue dataValue = root.value("data");
	if (!dataValue.isObject()) {
		set_error(response, "Invalid data object");
		return;
	}

	if (!vflow::set_template_parameters(sid, dataValue.toObject())) {
		set_error(response, "Failed to set template parameters");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_t *dataObj = obs_data_create_from_json(
		QJsonDocument(dataValue.toObject()).toJson(QJsonDocument::Compact).constData());
	if (dataObj) {
		obs_data_set_obj(response, "data", dataObj);
		obs_data_release(dataObj);
	}
}

static void req_GetTemplateParameters(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	QJsonObject root;
	if (!parse_json_object(request, root)) {
		set_error(response, "Invalid request payload");
		return;
	}

	const std::string sid = sanitize_id_local(root.value("id").toString().toStdString());
	if (sid.empty() || !vflow::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	QJsonObject dataObj;
	if (!vflow::get_template_parameters(sid, dataObj)) {
		set_error(response, "Failed to read template parameters");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_t *data = obs_data_create_from_json(QJsonDocument(dataObj).toJson(QJsonDocument::Compact).constData());
	if (data) {
		obs_data_set_obj(response, "data", data);
		obs_data_release(data);
	}
}

static void req_ToggleVisible(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	std::string sid = sanitize_id_local(idC ? idC : "");

	if (sid.empty() || !vflow::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	if (!vflow::toggle_visible_persist(sid)) {
		set_error(response, "Failed to toggle visibility");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "visible", vflow::is_visible(sid));
}

static void req_CreateLowerThird(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	if (!vflow::has_output_dir()) {
		set_error(response, "No output dir configured");
		return;
	}

	const std::string id = vflow::add_default_lower_third(true);
	if (id.empty()) {
		set_error(response, "Failed to create lower third");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", id.c_str());
	obs_data_set_int(response, "count", (long long)vflow::all_const().size());
}

static void req_CloneLowerThird(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	std::string sid = sanitize_id_local(idC ? idC : "");

	if (sid.empty() || !vflow::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	if (!vflow::has_output_dir()) {
		set_error(response, "No output dir configured");
		return;
	}

	const std::string newId = vflow::clone_lower_third(sid, true);
	if (newId.empty()) {
		set_error(response, "Failed to clone lower third");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_string(response, "newId", newId.c_str());
	obs_data_set_int(response, "count", (long long)vflow::all_const().size());
}

static void req_DeleteLowerThird(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	std::string sid = sanitize_id_local(idC ? idC : "");

	if (sid.empty() || !vflow::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	const bool ok = vflow::remove_lower_third(sid);
	if (!ok) {
		set_error(response, "Failed to delete lower third");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "removed", true);
	obs_data_set_int(response, "count", (long long)vflow::all_const().size());
}

static void req_ReloadFromDisk(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	if (!vflow::has_output_dir()) {
		set_error(response, "No output dir configured");
		return;
	}

	const bool ok = vflow::reload_from_disk_and_rebuild();

	set_ok(response, ok);
	obs_data_set_bool(response, "reloaded", ok);
	obs_data_set_int(response, "count", (long long)vflow::all_const().size());
}

// -------------------------
// UI-thread dispatch
// -------------------------
// obs-websocket invokes vendor requests on its own thread, while all plugin state
// is owned by the UI thread. Every request therefore runs on the UI thread; the
// websocket thread waits for the result with a timeout so a busy or exiting UI can
// never deadlock obs-websocket.
using request_fn = void (*)(obs_data_t *request, obs_data_t *response, void *priv);

struct pending_call {
	std::mutex mx;
	std::condition_variable cv;
	bool done = false;
	bool cancelled = false; // the caller gave up waiting: do not run the request
	obs_data_t *result = nullptr;

	~pending_call() { obs_data_release(result); }
};

static void dispatch(obs_data_t *request, obs_data_t *response, request_fn fn)
{
	if (!g_accepting) {
		set_error(response, "VinciFlow is not ready");
		return;
	}

	QCoreApplication *app = QCoreApplication::instance();
	if (!app || QThread::currentThread() == app->thread()) {
		fn(request, response, nullptr);
		return;
	}

	auto call = std::make_shared<pending_call>();
	obs_data_t *req = obs_data_create();
	obs_data_apply(req, request);

	QMetaObject::invokeMethod(
		app,
		[call, req, fn]() {
			bool cancelled;
			{
				std::lock_guard<std::mutex> lk(call->mx);
				cancelled = call->cancelled;
			}

			obs_data_t *res = obs_data_create();
			if (g_accepting && !cancelled)
				fn(req, res, nullptr);
			else
				set_error(res, "VinciFlow is shutting down");
			obs_data_release(req);

			std::lock_guard<std::mutex> lk(call->mx);
			call->result = res;
			call->done = true;
			call->cv.notify_all();
		},
		Qt::QueuedConnection);

	std::unique_lock<std::mutex> lk(call->mx);
	if (!call->cv.wait_for(lk, std::chrono::seconds(5), [&]() { return call->done; })) {
		// Never apply a request the client was told failed (avoids duplicates on retry).
		call->cancelled = true;
		set_error(response, "Timed out waiting for OBS");
		return;
	}
	obs_data_apply(response, call->result);
}

template<request_fn Fn> static void ui_request(obs_data_t *request, obs_data_t *response, void *)
{
	dispatch(request, response, Fn);
}

static const struct {
	const char *name;
	obs_websocket_request_callback_function fn;
} kRequests[] = {
	{"ListLowerThirds", ui_request<req_ListLowerThirds>},
	{"GetVisible", ui_request<req_GetVisible>},
	{"SetVisible", ui_request<req_SetVisible>},
	{"ToggleVisible", ui_request<req_ToggleVisible>},
	{"SetTemplateParameters", ui_request<req_SetTemplateParameters>},
	{"GetTemplateParameters", ui_request<req_GetTemplateParameters>},
	{"CreateLowerThird", ui_request<req_CreateLowerThird>},
	{"CloneLowerThird", ui_request<req_CloneLowerThird>},
	{"DeleteLowerThird", ui_request<req_DeleteLowerThird>},
	{"ReloadFromDisk", ui_request<req_ReloadFromDisk>},
};

// -------------------------
// Public init/shutdown
// -------------------------
bool init()
{
	if (g_vendor) {
		blog(LOG_INFO, LOG_TAG " ws already initialized");
		return true;
	}

	const unsigned int apiVer = obs_websocket_get_api_version();
	if (apiVer == 0) {
		blog(LOG_WARNING, LOG_TAG " obs-websocket API not available");
		return false;
	}

	g_vendor = obs_websocket_register_vendor(kVendorName);
	if (!g_vendor) {
		blog(LOG_WARNING, LOG_TAG " failed to register vendor '%s'", kVendorName);
		return false;
	}

	bool ok = true;
	for (const auto &r : kRequests)
		ok = obs_websocket_vendor_register_request(g_vendor, r.name, r.fn, nullptr) && ok;
	g_accepting = true;

	// Subscribe to core events AFTER vendor is ready
	g_core_listener_token = vflow::add_event_listener(on_core_event, nullptr);

	blog(LOG_INFO, LOG_TAG " vendor '%s' registered (api v%u) ok=%s", kVendorName, apiVer, ok ? "true" : "false");
	return ok;
}

void shutdown()
{
	g_accepting = false;
	if (g_vendor) {
		for (const auto &r : kRequests)
			obs_websocket_vendor_unregister_request(g_vendor, r.name);
	}

	if (g_core_listener_token) {
		vflow::remove_event_listener(g_core_listener_token);
		g_core_listener_token = 0;
	}
	g_vendor = nullptr;
}

} // namespace vflow::ws
