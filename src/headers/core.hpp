// core.hpp
#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <obs.h>
#include <obs-module.h>

#include "config.hpp"

#ifndef LOG_TAG
#define LOG_TAG "[" PLUGIN_NAME "]"
#endif

#define LOGI(fmt, ...) blog(LOG_INFO,    LOG_TAG " " fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) blog(LOG_WARNING, LOG_TAG " " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) blog(LOG_ERROR,   LOG_TAG " " fmt, ##__VA_ARGS__)

#if !defined(NDEBUG) || defined(ENABLE_LOG_DEBUG)
#define LOGD(fmt, ...) blog(LOG_INFO, LOG_TAG " [D] " fmt, ##__VA_ARGS__)
#else
#define LOGD(...) do {} while (0)
#endif

inline constexpr const char *sltBrowserSourceId = "browser_source";
inline constexpr const char *sltBrowserSourceName = "Smart Lower Thirds";
inline constexpr int sltBrowserWidth = 1920;
inline constexpr int sltBrowserHeight = 1080;

inline constexpr const char *sltDockId = "smart_lt_dock";
inline constexpr const char *sltDockTitle = "Smart Lower Thirds";

namespace smart_lt {

struct lower_third_cfg {
	std::string id;
	// Display-only label for the dock list (does not affect overlay content)
	std::string label;
	// Sort key for arranging items in the dock (lower first)
	int order = 0;

	std::string title;
	std::string subtitle;
	std::string profile_picture;

	// Optional audio cues (copied into output_dir; stored as filename)
	std::string anim_in_sound;
	std::string anim_out_sound;

	// Optional font sizes (px). Used by {{TITLE_SIZE}} / {{SUBTITLE_SIZE}} placeholders.
	int title_size = 46;
	int subtitle_size = 24;

	// Optional avatar size (px). Used by {{AVATAR_WIDTH}} / {{AVATAR_HEIGHT}} placeholders.
	int avatar_width = 100;
	int avatar_height = 100;

	std::string anim_in;  // animate.css class OR "custom_handled_in"
	std::string anim_out; // animate.css class OR "custom_handled_out"

	std::string font_family;
	std::string lt_position; // class name: e.g. "lt-pos-bottom-left"

	std::string primary_color;
	std::string secondary_color;
	std::string title_color;
	std::string subtitle_color;
	int opacity = 85; // 0..100
	int radius  = 5;  // 0..100

	std::string html_template; // inner HTML for <li id="{{ID}}">
	std::string css_template;  // should be scoped to #{{ID}} (we also do best-effort)
	std::string js_template;   // wrapped with root = document.getElementById("{{ID}}")

	std::string hotkey;

	int repeat_every_sec   = 0; // 0 = disabled
	int repeat_visible_sec = 0; // how long to keep visible when auto-shown
};


struct carousel_cfg {
	std::string id;
	std::string title;
	int order = 0;

	// Item ordering when running the carousel
	// 0 = Linear (in member list order)
	// 1 = Randomized (shuffled per run / cycle)
	int order_mode = 0;

	// If true, carousel repeats indefinitely. If false, it stops after the last item is shown once.
	bool loop = true;

	// Timing controls (milliseconds)
	// Defaults:
	//  - visible_ms:  15000 (how long a lower third stays visible)
	//  - interval_ms: 5000  (time between activating the next lower third)
	int visible_ms  = 15000;
	int interval_ms = 5000;

	// Dock-only color for marking items in this carousel (e.g. "#2EA043")
	std::string dock_color;

	// Member lower-third IDs (in display order)
	std::vector<std::string> members;
};

// -------------------------
// Core event bus (bidirectional sync point)
// -------------------------
enum class event_type : uint32_t {
	VisibilityChanged = 1,
	ListChanged       = 2,
	Reloaded          = 3,
};

enum class list_change_reason : uint32_t {
	Unknown = 0,
	Create  = 1,
	Clone   = 2,
	Delete  = 3,
	Reload  = 4,
	Update  = 5,
};

struct core_event {
	event_type type = event_type::VisibilityChanged;

	// VisibilityChanged
	std::string id;
	bool visible = false;
	std::vector<std::string> visible_ids;

	// ListChanged / Reloaded
	list_change_reason reason = list_change_reason::Unknown;
	std::string id2;
	bool ok = true;
	int64_t count = 0;
};

using core_event_cb = void (*)(const core_event &ev, void *user);

uint64_t add_event_listener(core_event_cb cb, void *user);
void remove_event_listener(uint64_t token);

// -------------------------
// Output dir + startup
// -------------------------
bool has_output_dir();
std::string output_dir();
bool set_output_dir_and_load(const std::string &dir);

// Reads OBS module config: obs_module_config_path("config.json")
void init_from_disk();

// Save OBS module config: obs_module_config_path("config.json")
bool save_global_config();

// -------------------------
// State access
// -------------------------
std::vector<lower_third_cfg> &all();
const std::vector<lower_third_cfg> &all_const();
lower_third_cfg *get_by_id(const std::string &id);

// -------------------------
// Carousel state access (persisted in lt-state.json; dock-only)
// -------------------------
std::vector<carousel_cfg> &carousels();
const std::vector<carousel_cfg> &carousels_const();
carousel_cfg *get_carousel_by_id(const std::string &id);
std::vector<std::string> carousels_containing(const std::string &lower_third_id);

// CRUD helpers for dock actions (persist + notify)
std::string add_default_carousel();
bool update_carousel(const carousel_cfg &c);
bool remove_carousel(const std::string &carousel_id);
bool set_carousel_members(const std::string &carousel_id, const std::vector<std::string> &members);


// -------------------------
// Visible set
// -------------------------
std::vector<std::string> visible_ids();
bool is_visible(const std::string &id);

// NOTE: low-level (no persistence, no notifications)
void set_visible_nosave(const std::string &id, bool visible);
void toggle_visible_nosave(const std::string &id);

// High-level (persist + notify)
bool set_visible_persist(const std::string &id, bool visible);
bool toggle_visible_persist(const std::string &id);

// -------------------------
// Persistence
// -------------------------
bool load_state_json();
bool save_state_json();
bool load_visible_json();
bool save_visible_json();

// -------------------------
// Artifacts files
// -------------------------
bool ensure_output_artifacts_exist();
bool rebuild_and_swap();

// Notify UI listeners (dock, websocket bridge, etc.) that the lower-third list
// has been updated in-place (e.g. settings changed for an existing item).
// This does not rebuild artifacts; it only emits a core event.
void notify_list_updated(const std::string &id = std::string());

// Force reload state+visible from disk and rebuild/swap (with notifications)
bool reload_from_disk_and_rebuild();

// -------------------------
// Browser source
// -------------------------
std::vector<std::string> list_browser_source_names();
std::string target_browser_source_name();
bool set_target_browser_source_name(const std::string &name);
bool target_browser_source_exists();
bool swap_target_browser_source_to_file(const std::string &absoluteHtmlPath);

// Browser source dimensions (persisted in module config)
int target_browser_width();
int target_browser_height();
bool set_target_browser_dimensions(int width, int height);

// Dock behavior (persisted in module config)
bool dock_exclusive_mode();
bool set_dock_exclusive_mode(bool enabled);

// -------------------------
// Paths
// -------------------------
std::string path_state_json();   // lt-state.json
std::string path_visible_json(); // lt-visible.json
std::string path_styles_css();   // lt.css
std::string path_scripts_js();   // lt.js
std::string path_animate_css();  // animate.min.css

// -------------------------
// Utility
// -------------------------
std::string now_timestamp_string();
std::string new_id();

// -------------------------
// CRUD helpers for dock actions (persist + notify)
// -------------------------
std::string add_default_lower_third();
std::string clone_lower_third(const std::string &id);
bool remove_lower_third(const std::string &id);

// Reorder helpers (persist + notify). delta: -1 (up), +1 (down)
bool move_lower_third(const std::string &id, int delta);

} // namespace smart_lt