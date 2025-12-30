#define LOG_TAG "[" PLUGIN_NAME "][ws]"
#include "websocket_bridge.hpp"

#include "core.hpp"

// vendored header
#include "thirdparty/obs-websocket-api.h"

#include <obs-module.h>
#include <obs.h>

#include <algorithm>
#include <string>
#include <vector>

namespace smart_lt::ws {

static obs_websocket_vendor g_vendor = nullptr;
static const char *kVendorName = "smart-lower-thirds";
static uint64_t g_core_listener_token = 0;

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

static const char *reason_to_str(smart_lt::list_change_reason r)
{
	using R = smart_lt::list_change_reason;
	switch (r) {
	case R::Create: return "create";
	case R::Clone:  return "clone";
	case R::Delete: return "delete";
	case R::Reload: return "reload";
	case R::Update: return "update";
	default:        return "unknown";
	}
}

// -------------------------
// CORE -> WS vendor events (single source of truth)
// -------------------------
static void on_core_event(const smart_lt::core_event &ev, void *user)
{
	UNUSED_PARAMETER(user);
	if (!g_vendor)
		return;

	if (ev.type == smart_lt::event_type::VisibilityChanged) {
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

	if (ev.type == smart_lt::event_type::ListChanged) {
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

	if (ev.type == smart_lt::event_type::Reloaded) {
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

	for (const auto &c : smart_lt::all_const()) {
		obs_data_t *it = obs_data_create();
		obs_data_set_string(it, "id", c.id.c_str());
		obs_data_set_string(it, "title", c.title.c_str());
		obs_data_set_string(it, "subtitle", c.subtitle.c_str());
		obs_data_set_bool(it, "isVisible", smart_lt::is_visible(c.id));
		obs_data_set_int(it, "repeatEverySec", c.repeat_every_sec);
		obs_data_set_int(it, "repeatVisibleSec", c.repeat_visible_sec);
		obs_data_set_string(it, "hotkey", c.hotkey.c_str());

		obs_data_set_string(it, "bgColor", c.bg_color.c_str());
		obs_data_set_string(it, "textColor", c.text_color.c_str());
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
	for (const auto &id : smart_lt::visible_ids()) {
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
	if (sid.empty() || !smart_lt::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	if (!smart_lt::set_visible_persist(sid, visible)) {
		set_error(response, "Failed to set visibility");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "visible", smart_lt::is_visible(sid));
}

static void req_ToggleVisible(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	std::string sid = sanitize_id_local(idC ? idC : "");

	if (sid.empty() || !smart_lt::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	if (!smart_lt::toggle_visible_persist(sid)) {
		set_error(response, "Failed to toggle visibility");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "visible", smart_lt::is_visible(sid));
}

static void req_CreateLowerThird(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	if (!smart_lt::has_output_dir()) {
		set_error(response, "No output dir configured");
		return;
	}

	const std::string id = smart_lt::add_default_lower_third();
	if (id.empty()) {
		set_error(response, "Failed to create lower third");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", id.c_str());
	obs_data_set_int(response, "count", (long long)smart_lt::all_const().size());
}

static void req_CloneLowerThird(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	std::string sid = sanitize_id_local(idC ? idC : "");

	if (sid.empty() || !smart_lt::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	if (!smart_lt::has_output_dir()) {
		set_error(response, "No output dir configured");
		return;
	}

	const std::string newId = smart_lt::clone_lower_third(sid);
	if (newId.empty()) {
		set_error(response, "Failed to clone lower third");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_string(response, "newId", newId.c_str());
	obs_data_set_int(response, "count", (long long)smart_lt::all_const().size());
}

static void req_DeleteLowerThird(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(priv);

	const char *idC = obs_data_get_string(request, "id");
	std::string sid = sanitize_id_local(idC ? idC : "");

	if (sid.empty() || !smart_lt::get_by_id(sid)) {
		set_error(response, "Invalid id");
		return;
	}

	const bool ok = smart_lt::remove_lower_third(sid);
	if (!ok) {
		set_error(response, "Failed to delete lower third");
		return;
	}

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "removed", true);
	obs_data_set_int(response, "count", (long long)smart_lt::all_const().size());
}

static void req_ReloadFromDisk(obs_data_t *request, obs_data_t *response, void *priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	if (!smart_lt::has_output_dir()) {
		set_error(response, "No output dir configured");
		return;
	}

	const bool ok = smart_lt::reload_from_disk_and_rebuild();

	set_ok(response, ok);
	obs_data_set_bool(response, "reloaded", ok);
	obs_data_set_int(response, "count", (long long)smart_lt::all_const().size());
}

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

	ok = ok && obs_websocket_vendor_register_request(g_vendor, "ListLowerThirds", req_ListLowerThirds, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "GetVisible", req_GetVisible, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "SetVisible", req_SetVisible, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "ToggleVisible", req_ToggleVisible, nullptr);

	ok = ok && obs_websocket_vendor_register_request(g_vendor, "CreateLowerThird", req_CreateLowerThird, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "CloneLowerThird", req_CloneLowerThird, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "DeleteLowerThird", req_DeleteLowerThird, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "ReloadFromDisk", req_ReloadFromDisk, nullptr);

	// Subscribe to core events AFTER vendor is ready
	g_core_listener_token = smart_lt::add_event_listener(on_core_event, nullptr);

	blog(LOG_INFO, LOG_TAG " vendor '%s' registered (api v%u) ok=%s", kVendorName, apiVer, ok ? "true" : "false");
	return ok;
}

void shutdown()
{
	if (g_core_listener_token) {
		smart_lt::remove_event_listener(g_core_listener_token);
		g_core_listener_token = 0;
	}
	g_vendor = nullptr;
}

} // namespace smart_lt::ws
