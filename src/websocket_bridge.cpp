#define LOG_TAG "[" PLUGIN_NAME "][ws]"
#include "websocket_bridge.hpp"

#include "core.hpp"

// Use your vendored header path:
#include "thirdparty/obs-websocket-api.h"

#include <obs-module.h>
#include <obs.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace smart_lt::ws {

static obs_websocket_vendor g_vendor = nullptr;
static const char *kVendorName = "smart-lower-thirds";

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

// Emit vendor event: LowerThirdsVisibilityChanged
static void emit_visibility_changed(const char *id, bool visible)
{
	if (!g_vendor)
		return;

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", id ? id : "");
	obs_data_set_bool(data, "visible", visible);

	// include full visible list for convenience
	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &vid : smart_lt::visible_ids()) {
		obs_data_t *o = obs_data_create();
		obs_data_set_string(o, "id", vid.c_str());
		obs_data_array_push_back(arr, o);
		obs_data_release(o);
	}
	obs_data_set_array(data, "visibleIds", arr);
	obs_data_array_release(arr);

	obs_websocket_vendor_emit_event(g_vendor, "LowerThirdsVisibilityChanged", data);
	obs_data_release(data);
}

// -------------------------
// Vendor request callbacks
// Signature: (obs_data_t *request, obs_data_t *response, void *priv)
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

	const bool before = smart_lt::is_visible(sid);
	if (before != visible) {
		smart_lt::set_visible(sid, visible);
		smart_lt::save_visible_json();
		emit_visibility_changed(sid.c_str(), visible);
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

	const bool before = smart_lt::is_visible(sid);

	smart_lt::toggle_visible(sid);
	smart_lt::save_visible_json();

	const bool after = smart_lt::is_visible(sid);
	if (after != before)
		emit_visibility_changed(sid.c_str(), after);

	set_ok(response, true);
	obs_data_set_string(response, "id", sid.c_str());
	obs_data_set_bool(response, "visible", after);
}

// -------------------------
// Public init/shutdown
// -------------------------

bool init()
{
	// The header warns: vendor registration should occur in obs_module_post_load().
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

	// IMPORTANT: this API takes 4 args (vendor, type, callback, priv_data)
	bool ok = true;
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "ListLowerThirds", req_ListLowerThirds, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "GetVisible", req_GetVisible, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "SetVisible", req_SetVisible, nullptr);
	ok = ok && obs_websocket_vendor_register_request(g_vendor, "ToggleVisible", req_ToggleVisible, nullptr);

	blog(LOG_INFO, LOG_TAG " vendor '%s' registered (api v%u) ok=%s", kVendorName, apiVer, ok ? "true" : "false");
	return ok;
}

void shutdown()
{
	// If you want to unregister requests explicitly, do it here.
	// This header exposes obs_websocket_vendor_unregister_request; you can call it.
	// Most plugins simply let OBS tear down at process shutdown.
	g_vendor = nullptr;
}

} // namespace smart_lt::ws
