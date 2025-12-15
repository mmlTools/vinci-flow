#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

inline constexpr const char *sltDockId = "SmartLowerThirdsDock";
inline constexpr const char *sltDockTitle = "Smart Lower Thirds";

struct CbxOption {
	const char *label;
	const char *value;
};

inline const std::vector<CbxOption> AnimInOptions = {
	{"Fade In", "animate__fadeIn"},
	{"Fade In Up", "animate__fadeInUp"},
	{"Fade In Down", "animate__fadeInDown"},
	{"Fade In Left", "animate__fadeInLeft"},
	{"Fade In Right", "animate__fadeInRight"},
	{"Back In Up", "animate__backInUp"},
	{"Back In Down", "animate__backInDown"},
	{"Back In Left", "animate__backInLeft"},
	{"Back In Right", "animate__backInRight"},
	{"Bounce In", "animate__bounceIn"},
	{"Zoom In", "animate__zoomIn"},
	{"Slide In Up", "animate__slideInUp"},
	{"Slide In Down", "animate__slideInDown"},
	{"Slide In Left", "animate__slideInLeft"},
	{"Slide In Right", "animate__slideInRight"},
	{"Flip In X", "animate__flipInX"},
	{"Flip In Y", "animate__flipInY"},
	{"Jack In The Box", "animate__jackInTheBox"},
	{"Custom (CSS class)", "custom"},
};

inline const std::vector<CbxOption> AnimOutOptions = {
	{"Fade Out", "animate__fadeOut"},
	{"Fade Out Up", "animate__fadeOutUp"},
	{"Fade Out Down", "animate__fadeOutDown"},
	{"Fade Out Left", "animate__fadeOutLeft"},
	{"Fade Out Right", "animate__fadeOutRight"},
	{"Back Out Up", "animate__backOutUp"},
	{"Back Out Down", "animate__backOutDown"},
	{"Back Out Left", "animate__backOutLeft"},
	{"Back Out Right", "animate__backOutRight"},
	{"Bounce Out", "animate__bounceOut"},
	{"Zoom Out", "animate__zoomOut"},
	{"Slide Out Up", "animate__slideOutUp"},
	{"Slide Out Down", "animate__slideOutDown"},
	{"Slide Out Left", "animate__slideOutLeft"},
	{"Slide Out Right", "animate__slideOutRight"},
	{"Flip Out X", "animate__flipOutX"},
	{"Flip Out Y", "animate__flipOutY"},
	{"Roll Out", "animate__rollOut"},
	{"Custom (CSS class)", "custom"},
};

inline const std::vector<CbxOption> LtPositionOptions = {
	{"Bottom Left", "lt-pos-bottom-left"}, {"Bottom Right", "lt-pos-bottom-right"}, {"Top Left", "lt-pos-top-left"},
	{"Top Right", "lt-pos-top-right"},     {"Screen Center", "lt-pos-center"},
};

struct LowerThirdConfig {
	std::string id;
	std::string title;
	std::string subtitle;

	std::string anim_in;
	std::string anim_out;
	std::string custom_anim_in;
	std::string custom_anim_out;
	std::string lt_position;

	std::string font_family;

	// Stored as "#RRGGBB" or "#AARRGGBB"
	std::string bg_color;
	std::string text_color;

	std::string html_template;
	std::string css_template;

	// Optional UI-only hint; used to create app-wide QShortcuts in the dock.
	// Stored as QKeySequence::PortableText string.
	std::string hotkey;

	bool visible = false;

	// File name stored relative to output_dir
	std::string profile_picture;
};

namespace smart_lt {

// Your workflow toggle
enum class ApplyMode {
	JsonOnly,       // minor changes only -> save JSON, do not bump rev
	HtmlAndJsonRev, // major changes -> rev++, rewrite HTML (embedded HTML_REV), save JSON, reload sources
};

void init_from_disk();

// output directory
void set_output_dir(const std::string &dir);
bool has_output_dir();
const std::string &output_dir();
const std::string &index_html_path(); // absolute HTML path
std::string state_json_path();        // absolute JSON path

// Switch folder + load state if present + ensure artifacts + repoint managed browser sources.
// Returns true if the folder exists and output_dir is usable.
bool set_output_dir_and_load(const std::string &dir);

// items
std::vector<LowerThirdConfig> &all();
LowerThirdConfig *get_by_id(const std::string &id);

// CRUD (major)
std::string add_default_lower_third();
std::string clone_lower_third(const std::string &id);
void remove_lower_third(const std::string &id);

// visibility (minor)
void toggle_active(const std::string &id, bool hideOthers = true);
void set_active_exact(const std::string &id);

// persistence
bool load_state_json();
bool save_state_json();

// writes HTML using current g_rev (does NOT bump rev by itself)
bool write_index_html();

// single source of truth apply
void apply_changes(ApplyMode mode);

// Ensure JSON/HTML exist in output folder without bumping rev.
// Returns true if it created/wrote anything.
bool ensure_output_artifacts_exist();

// browser source maintenance for managed sources
void repoint_managed_browser_sources(bool reload);

} // namespace smart_lt
