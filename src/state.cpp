#include "state.hpp"
#include "config.hpp"
#include "const.hpp"
#include "log.hpp"
#include "entities.hpp"
#include "server.hpp"
#include "slt_helpers.hpp"

#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-data.h>
#include <util/platform.h>

#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>

namespace smart_lt {

static std::vector<LowerThirdConfig> g_items;
static std::string g_output_dir;
static std::string g_index_html_path;
static int g_preferred_port = 8089;

static std::string get_global_config_path()
{
	static std::string config_path;
	if (config_path.empty()) {
		char *p = obs_module_config_path("config.json");
		if (p) {
			config_path = p;
			LOGI("Global config path resolved to '%s'", config_path.c_str());
			bfree(p);
		} else {
			LOGW("obs_module_config_path returned null for config.json");
		}
	}
	return config_path;
}

static void save_global_config()
{
	std::string path = get_global_config_path();
	if (path.empty())
		return;

	size_t sep = path.find_last_of("/\\");
	if (sep != std::string::npos) {
		std::string dir = path.substr(0, sep);
		int mk = os_mkdirs(dir.c_str());
		if (mk != 0) {
			LOGW("Failed to create config directory '%s' (os_mkdirs=%d)", dir.c_str(), mk);
		}
	}

	obs_data_t *root = obs_data_create();
	if (!root) {
		LOGW("Failed to create obs_data for global config");
		return;
	}

	obs_data_set_string(root, "output_dir", g_output_dir.c_str());
	obs_data_set_int(root, "preferred_port", g_preferred_port);

	int rc = obs_data_save_json(root, path.c_str());
	obs_data_release(root);

	if (rc != 0) {
		LOGW("obs_data_save_json failed for '%s' (code=%d)", path.c_str(), rc);
	} else {
		LOGI("Saved global config to '%s' (output_dir='%s', preferred_port=%d)", path.c_str(),
		     g_output_dir.c_str(), g_preferred_port);
	}
}

static void load_global_config()
{
	std::string path = get_global_config_path();
	if (path.empty())
		return;

	LOGI("Trying to load global config from '%s'", path.c_str());

	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root) {
		LOGW("No global config found at '%s'", path.c_str());
		return;
	}

	const char *dir = obs_data_get_string(root, "output_dir");
	if (dir && *dir) {
		g_output_dir = dir;
		LOGI("Loaded output_dir from global config: '%s'", g_output_dir.c_str());
	}

	int p = (int)obs_data_get_int(root, "preferred_port");
	if (p > 0 && p < 65536)
		g_preferred_port = p;

	obs_data_release(root);
}

static void update_index_path_from_output_dir()
{
	if (g_output_dir.empty()) {
		g_index_html_path.clear();
		return;
	}

	g_index_html_path = g_output_dir;
	char last = g_index_html_path.back();
	if (last != '/' && last != '\\')
		g_index_html_path += '/';
	g_index_html_path += "smart-lower-thirds.html";
}

void set_output_dir(const std::string &dir)
{
	g_output_dir = dir;
	update_index_path_from_output_dir();
	save_global_config();

	if (!g_output_dir.empty()) {
		LOGI("Output dir set to '%s', index.html = '%s'", g_output_dir.c_str(), g_index_html_path.c_str());
	} else {
		LOGW("Output dir cleared; index.html will not be written");
	}
}

bool has_output_dir()
{
	return !g_index_html_path.empty();
}

const std::string &output_dir()
{
	return g_output_dir;
}

const std::string &index_html_path()
{
	return g_index_html_path;
}

int get_preferred_port()
{
	return g_preferred_port;
}

void set_preferred_port(int port)
{
	if (port <= 0 || port >= 65536)
		return;
	g_preferred_port = port;
	save_global_config();
}

std::vector<LowerThirdConfig> &all()
{
	return g_items;
}

static std::string make_new_id()
{
	size_t idx = g_items.size() + 1;
	std::string id;
	bool unique = false;

	while (!unique) {
		id = "lower-third-" + std::to_string(idx);
		unique = true;
		for (const auto &it : g_items) {
			if (it.id == id) {
				unique = false;
				++idx;
				break;
			}
		}
	}
	return id;
}

std::string add_default_lower_third()
{
	LowerThirdConfig cfg;
	cfg.id = make_new_id();
	cfg.title = "Title";
	cfg.subtitle = "Subtitle";
	cfg.anim_in = "animate__fadeInUp";
	cfg.anim_out = "animate__fadeOutDown";
	cfg.font_family = "Inter";
	cfg.bg_color = "#C0000000";
	cfg.text_color = "#FFFFFFFF";
	cfg.bound_scene.clear();
	cfg.hotkey.clear();
	cfg.visible = false;
	cfg.profile_picture.clear();

	cfg.html_template = "<li id=\"{{ID}}\" class=\"lower-third animate__animated\">\n"
			    "  <div class=\"lt-inner\">\n"
			    "    <div class=\"lt-title\">{{TITLE}}</div>\n"
			    "    <div class=\"lt-subtitle\">{{SUBTITLE}}</div>\n"
			    "  </div>\n"
			    "</li>\n";

	cfg.css_template = "#{{ID}} .lt-inner {\n"
			   "  font-family: {{FONT_FAMILY}}, sans-serif;\n"
			   "  background: {{BG_COLOR}};\n"
			   "  color: {{TEXT_COLOR}};\n"
			   "  padding: 12px 24px;\n"
			   "  border-radius: 8px;\n"
			   "}\n"
			   "#{{ID}} .lt-title {\n"
			   "  font-size: 28px;\n"
			   "  font-weight: 700;\n"
			   "}\n"
			   "#{{ID}} .lt-subtitle {\n"
			   "  font-size: 18px;\n"
			   "  opacity: 0.85;\n"
			   "}\n";

	g_items.push_back(cfg);
	return cfg.id;
}

std::string clone_lower_third(const std::string &id)
{
	for (const auto &it : g_items) {
		if (it.id == id) {
			LowerThirdConfig copy = it;
			copy.id = make_new_id();
			copy.title += " (Copy)";
			copy.visible = false;
			g_items.push_back(copy);
			return copy.id;
		}
	}
	return {};
}

void remove_lower_third(const std::string &id)
{
	for (auto it = g_items.begin(); it != g_items.end(); ++it) {
		if (it->id == id) {
			g_items.erase(it);
			break;
		}
	}
}

LowerThirdConfig *get_by_id(const std::string &id)
{
	for (auto &it : g_items) {
		if (it.id == id)
			return &it;
	}
	return nullptr;
}

void toggle_active(const std::string &id)
{
	bool thisVisible = false;
	bool thisIsOnlyVisible = true;

	for (const auto &it : g_items) {
		if (it.visible) {
			if (it.id != id)
				thisIsOnlyVisible = false;
			else
				thisVisible = true;
		}
	}

	if (g_items.empty())
		return;

	if (thisVisible && thisIsOnlyVisible) {
		for (auto &it : g_items)
			it.visible = false;
	} else {
		for (auto &it : g_items) {
			it.visible = (it.id == id);
		}
	}
}

void set_active_exact(const std::string &id)
{
	if (g_items.empty())
		return;

	if (id.empty()) {
		for (auto &it : g_items)
			it.visible = false;
		return;
	}

	bool found = false;
	for (auto &it : g_items) {
		if (it.id == id) {
			it.visible = true;
			found = true;
		} else {
			it.visible = false;
		}
	}

	if (!found) {
		for (auto &it : g_items)
			it.visible = false;
	}
}

void handle_scene_changed()
{
	obs_source_t *curSceneSrc = obs_frontend_get_current_scene();
	std::string sceneName;
	if (curSceneSrc) {
		const char *name = obs_source_get_name(curSceneSrc);
		if (name)
			sceneName = name;
		obs_source_release(curSceneSrc);
	}

	std::string newActiveId;
	for (auto &cfg : g_items) {
		if (!cfg.bound_scene.empty() && cfg.bound_scene == sceneName) {
			newActiveId = cfg.id;
			break;
		}
	}

	if (!newActiveId.empty())
		set_active_exact(newActiveId);
	else
		set_active_exact("");

	write_index_html();
	save_state_json();
}

static std::string replace_all(std::string text, const std::string &from, const std::string &to)
{
	if (from.empty())
		return text;

	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		text.replace(pos, from.length(), to);
		pos += to.length();
	}
	return text;
}

static std::string css_from_stored_color(const std::string &stored)
{
	if (stored.size() == 9 && stored[0] == '#') {
		auto hexByte = [&](int idx) -> int {
			char buf[3] = {stored[idx], stored[idx + 1], 0};
			char *end = nullptr;
			long v = strtol(buf, &end, 16);
			if (!end || *end)
				return 0;
			return int(v);
		};

		int a = hexByte(1);
		int r = hexByte(3);
		int g = hexByte(5);
		int b = hexByte(7);

		char out[64];
		snprintf(out, sizeof(out), "rgba(%d,%d,%d,%.3f)", r, g, b, a / 255.0);
		return out;
	}

	return stored;
}

bool write_index_html()
{
	if (!has_output_dir()) {
		LOGW("Output directory not set, skipping index.html generation");
		return false;
	}

	std::string html;
	html.reserve(8192);

	html += "<!doctype html>\n";
	html += "<html>\n<head>\n<meta charset=\"utf-8\" />\n";
	html += "<title>Smart Lower Thirds</title>\n";
	html += "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/animate.css/4.1.1/animate.min.css\" />\n";
	html += "<style>\n";
	html += "html,body{margin:0;padding:0;background:transparent;overflow:hidden;}\n";
	html += "ul#lower-thirds-root{list-style:none;margin:0;padding:0;}\n";
	html += ".lower-third{position:absolute;left:5%;bottom:5%;opacity:0;pointer-events:none;}\n";

	for (const auto &cfg : g_items) {
		std::string css = cfg.css_template;
		css = replace_all(css, "{{ID}}",          cfg.id);
		css = replace_all(css, "{{FONT_FAMILY}}", cfg.font_family);
		css = replace_all(css, "{{BG_COLOR}}",    css_from_stored_color(cfg.bg_color));
		css = replace_all(css, "{{TEXT_COLOR}}",  css_from_stored_color(cfg.text_color));
		html += css;
		html += "\n";
	}

	html += "</style>\n</head>\n<body>\n<ul id=\"lower-thirds-root\">\n";

	for (const auto &cfg : g_items) {
		std::string item = cfg.html_template;

		std::string animInTpl  = (cfg.anim_in  == "custom") ? cfg.custom_anim_in  : cfg.anim_in;
		std::string animOutTpl = (cfg.anim_out == "custom") ? cfg.custom_anim_out : cfg.anim_out;

		item = replace_all(item, "{{ID}}",              cfg.id);
		item = replace_all(item, "{{TITLE}}",           cfg.title);
		item = replace_all(item, "{{SUBTITLE}}",        cfg.subtitle);
		item = replace_all(item, "{{PROFILE_PICTURE}}", cfg.profile_picture);
		item = replace_all(item, "{{ANIM_IN}}",         animInTpl);
		item = replace_all(item, "{{ANIM_OUT}}",        animOutTpl);

		html += item;
		html += "\n";
	}

	html += "</ul>\n";

	std::string jsAllAnims = "  const ALL_ANIMS = [";
	bool firstAnim = true;

	auto appendAnim = [&](const char *cls) {
		if (!cls || !*cls)
			return;
		if (!firstAnim)
			jsAllAnims += ",";
		jsAllAnims += "'";
		jsAllAnims += cls;
		jsAllAnims += "'";
		firstAnim = false;
	};

	for (const auto &opt : AnimInOptions) {
		if (std::string(opt.value) != "custom")
			appendAnim(opt.value);
	}
	for (const auto &opt : AnimOutOptions) {
		if (std::string(opt.value) != "custom")
			appendAnim(opt.value);
	}

	jsAllAnims += "];\n";

	const char *defaultAnimIn  = !AnimInOptions.empty()  ? AnimInOptions.front().value  : "animate__fadeInUp";
	const char *defaultAnimOut = !AnimOutOptions.empty() ? AnimOutOptions.front().value : "animate__fadeOutDown";

	html += "<script>\n";
	html += "(function(){\n";
	html += "  const nodes = Array.from(document.querySelectorAll('.lower-third'));\n";
	html += "  const byId = {};\n";
	html += "  nodes.forEach(li => { if (li.id) byId[li.id] = li; });\n";
	html += jsAllAnims;
	html += "  let lastActiveId = null;\n";
	html += "  function clearAnim(li, cfg){\n";
	html += "    ALL_ANIMS.forEach(a => li.classList.remove(a));\n";
	html += "    if (cfg) {\n";
	html += "      if (cfg.anim_in)  li.classList.remove(cfg.anim_in);\n";
	html += "      if (cfg.anim_out) li.classList.remove(cfg.anim_out);\n";
	html += "    }\n";
	html += "  }\n";
	html += "  function applyState(activeId, animMap){\n";
	html += "    if (activeId === lastActiveId) return;\n";
	html += "    // animate OUT previous\n";
	html += "    if (lastActiveId && byId[lastActiveId]) {\n";
	html += "      const li = byId[lastActiveId];\n";
	html += "      const cfg = animMap[lastActiveId] || {};\n";
	html += "      const animOut = cfg.anim_out || '";
	html += defaultAnimOut;
	html += "';\n";
	html += "      clearAnim(li, cfg);\n";
	html += "      li.style.pointerEvents = 'none';\n";
	html += "      li.style.opacity = '1';\n";
	html += "      if (animOut) {\n";
	html += "        li.classList.add(animOut);\n";
	html += "        const handler = (ev) => {\n";
	html += "          li.removeEventListener('animationend', handler);\n";
	html += "          clearAnim(li, cfg);\n";
	html += "          li.style.opacity = '0';\n";
	html += "        };\n";
	html += "        li.addEventListener('animationend', handler);\n";
	html += "      } else {\n";
	html += "        li.style.opacity = '0';\n";
	html += "      }\n";
	html += "    }\n";
	html += "    // animate IN new\n";
	html += "    if (activeId && byId[activeId]) {\n";
	html += "      const li = byId[activeId];\n";
	html += "      const cfg = animMap[activeId] || {};\n";
	html += "      const animIn = cfg.anim_in || '";
	html += defaultAnimIn;
	html += "';\n";
	html += "      clearAnim(li, cfg);\n";
	html += "      li.style.opacity = '1';\n";
	html += "      li.style.pointerEvents = 'auto';\n";
	html += "      if (animIn) {\n";
	html += "        li.classList.add(animIn);\n";
	html += "      }\n";
	html += "    }\n";
	html += "    lastActiveId = activeId;\n";
	html += "  }\n";
	html += "  async function tick(){\n";
	html += "    try {\n";
	html += "      const res = await fetch('smart-lower-thirds-state.json?ts=' + Date.now(), { cache: 'no-store' });\n";
	html += "      if (!res.ok) return;\n";
	html += "      const data = await res.json();\n";
	html += "      const items = data.items || [];\n";
	html += "      let activeId = '';\n";
	html += "      const animMap = {};\n";
	html += "      for (const it of items) {\n";
	html += "        if (!it || !it.id) continue;\n";
	html += "        const customIn  = it.custom_anim_in  || '';\n";
	html += "        const customOut = it.custom_anim_out || '';\n";
	html += "        const effectiveIn  = (it.anim_in  === 'custom') ? customIn  : it.anim_in;\n";
	html += "        const effectiveOut = (it.anim_out === 'custom') ? customOut : it.anim_out;\n";
	html += "        animMap[it.id] = {\n";
	html += "          anim_in:  effectiveIn  || '',\n";
	html += "          anim_out: effectiveOut || ''\n";
	html += "        };\n";
	html += "        if (!activeId && it.visible)\n";
	html += "          activeId = it.id;\n";
	html += "      }\n";
	html += "      applyState(activeId, animMap);\n";
	html += "    } catch(e) {\n";
	html += "      // ignore\n";
	html += "    }\n";
	html += "  }\n";
	html += "  tick();\n";
	html += "  setInterval(tick, 500);\n";
	html += "})();\n";
	html += "</script>\n";

	html += "</body>\n</html>\n";

	std::ofstream file(g_index_html_path, std::ios::binary);
	if (!file.is_open()) {
		LOGE("Failed to open index.html for writing: '%s'", g_index_html_path.c_str());
		return false;
	}

	file.write(html.data(), static_cast<std::streamsize>(html.size()));
	file.close();

	if (!file) {
		LOGE("Failed to write index.html completely to '%s'", g_index_html_path.c_str());
		return false;
	}

	LOGI("Wrote index.html to '%s' (static HTML + JS polling)", g_index_html_path.c_str());
	return true;
}

static std::string make_browser_url()
{
	if (!server_is_running())
		return {};

	int port = server_port();
	if (port <= 0)
		return {};

	char buf[512];
	snprintf(buf, sizeof(buf), "http://127.0.0.1:%d/smart-lower-thirds.html", port);
	return std::string(buf);
}

void ensure_browser_source()
{
	if (!server_is_running())
		return;

	std::string url = make_browser_url();
	if (url.empty())
		return;

	obs_source_t *source = obs_get_source_by_name(sltBrowserSourceName);
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_bool(settings, "is_local_file", false);
		obs_data_set_string(settings, "url", url.c_str());
		obs_data_set_int(settings, "width", sltBrowserWidth);
		obs_data_set_int(settings, "height", sltBrowserHeight);

		source = obs_source_create(sltBrowserSourceId, sltBrowserSourceName, settings, nullptr);
		obs_data_release(settings);

		if (!source) {
			LOGE("Failed to create browser source '%s'", sltBrowserSourceName);
			return;
		}

		LOGI("Created browser source '%s' with url '%s'", sltBrowserSourceName, url.c_str());
	} else {
		obs_data_t *settings = obs_source_get_settings(source);
		if (settings) {
			const bool isLocal = obs_data_get_bool(settings, "is_local_file");
			if (!isLocal) {
				obs_data_set_string(settings, "url", url.c_str());
				obs_source_update(source, settings);
			}
			obs_data_release(settings);
		}
	}

	obs_source_release(source);
}

QString cache_bust_url(const QString &in)
{
	QUrl u(in);
	QUrlQuery q(u);
	q.removeAllQueryItems(QStringLiteral("cb"));
	q.addQueryItem(QStringLiteral("cb"), QString::number(QDateTime::currentMSecsSinceEpoch()));
	u.setQuery(q);
	return u.toString();
}

void refresh_browser_source()
{
	if (!server_is_running())
		return;

	std::string url = make_browser_url();
	if (url.empty())
		return;

	obs_source_t *src = obs_get_source_by_name(sltBrowserSourceName);
	if (!src) {
		LOGW("Browser source not found");
		return;
	}
	LOGI("Refreshing browser source '%s'", sltBrowserSourceName);

	const char *sid = obs_source_get_id(src);
	if (sid && strcmp(sid, "browser_source") == 0) {
		obs_data_t *settings = obs_source_get_settings(src);
		if (settings) {
			const bool isLocal = obs_data_get_bool(settings, "is_local_file");
			if (!isLocal) {
				const char *curl = obs_data_get_string(settings, "url");
				QString url = cache_bust_url(QString::fromUtf8(curl ? curl : ""));
				obs_data_set_string(settings, "url", url.toUtf8().constData());
				obs_source_update(src, settings);
			} else {
				bool shutdown = obs_data_get_bool(settings, "shutdown");
				obs_data_set_bool(settings, "shutdown", !shutdown);
				obs_source_update(src, settings);
				obs_data_set_bool(settings, "shutdown", shutdown);
				obs_source_update(src, settings);
			}
			obs_data_release(settings);
		}
	} else {
		LOGW("Source '%s' is not a browser_source", sltBrowserSourceName);
	}

	obs_source_release(src);
}

static std::string get_state_json_path()
{
	if (!has_output_dir())
		return {};
	std::string path = g_output_dir;
	char last = path.back();
	if (last != '/' && last != '\\')
		path += '/';
	path += "smart-lower-thirds-state.json";
	return path;
}

bool save_state_json()
{
	if (!has_output_dir())
		return false;

	std::string path = get_state_json_path();
	if (path.empty())
		return false;

	obs_data_t *root = obs_data_create();
	if (!root)
		return false;

	obs_data_set_string(root, "output_dir", g_output_dir.c_str());
	obs_data_set_int(root, "preferred_port", g_preferred_port);

	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &cfg : g_items) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "id", cfg.id.c_str());
		obs_data_set_string(item, "title", cfg.title.c_str());
		obs_data_set_string(item, "subtitle", cfg.subtitle.c_str());
		obs_data_set_string(item, "anim_in", cfg.anim_in.c_str());
		obs_data_set_string(item, "anim_out", cfg.anim_out.c_str());
		obs_data_set_string(item, "custom_anim_in", cfg.custom_anim_in.c_str());
		obs_data_set_string(item, "custom_anim_out", cfg.custom_anim_out.c_str());
		obs_data_set_string(item, "font_family", cfg.font_family.c_str());
		obs_data_set_string(item, "bg_color", cfg.bg_color.c_str());
		obs_data_set_string(item, "text_color", cfg.text_color.c_str());
		obs_data_set_string(item, "html_template", cfg.html_template.c_str());
		obs_data_set_string(item, "css_template", cfg.css_template.c_str());
		obs_data_set_string(item, "bound_scene", cfg.bound_scene.c_str());
		obs_data_set_bool(item, "visible", cfg.visible);
		obs_data_set_string(item, "hotkey", cfg.hotkey.c_str());
		obs_data_set_string(item, "profile_picture", cfg.profile_picture.c_str());

		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(root, "items", arr);
	obs_data_array_release(arr);

	obs_data_save_json(root, path.c_str());
	obs_data_release(root);

	LOGI("Saved state.json to '%s'", path.c_str());
	return true;
}

bool load_state_json()
{
	if (!has_output_dir())
		return false;

	std::string path = get_state_json_path();
	if (path.empty())
		return false;

	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root)
		return false;

	const char *outdir = obs_data_get_string(root, "output_dir");
	if (outdir && *outdir) {
		set_output_dir(outdir);
	}

	int pp = (int)obs_data_get_int(root, "preferred_port");
	if (pp > 0 && pp < 65536)
		g_preferred_port = pp;

	g_items.clear();

	obs_data_array_t *arr = obs_data_get_array(root, "items");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(arr, i);
			if (!item)
				continue;

			LowerThirdConfig cfg;
			cfg.id = obs_data_get_string(item, "id");
			cfg.title = obs_data_get_string(item, "title");
			cfg.subtitle = obs_data_get_string(item, "subtitle");
			cfg.anim_in = obs_data_get_string(item, "anim_in");
			cfg.anim_out = obs_data_get_string(item, "anim_out");
			cfg.custom_anim_in = obs_data_get_string(item, "custom_anim_in");
			cfg.custom_anim_out = obs_data_get_string(item, "custom_anim_out");
			cfg.font_family = obs_data_get_string(item, "font_family");
			cfg.bg_color = obs_data_get_string(item, "bg_color");
			cfg.text_color = obs_data_get_string(item, "text_color");
			cfg.html_template = obs_data_get_string(item, "html_template");
			cfg.css_template = obs_data_get_string(item, "css_template");
			cfg.bound_scene = obs_data_get_string(item, "bound_scene");
			cfg.visible = obs_data_get_bool(item, "visible");
			cfg.hotkey = obs_data_get_string(item, "hotkey");
			cfg.profile_picture = obs_data_get_string(item, "profile_picture");

			g_items.push_back(cfg);

			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}

	obs_data_release(root);
	LOGI("Loaded state.json from '%s' (%zu items)", path.c_str(), g_items.size());
	return true;
}

void init_state_from_disk()
{
	load_global_config();
	if (!g_output_dir.empty())
		update_index_path_from_output_dir();

	if (has_output_dir()) {
		if (!load_state_json()) {
			if (g_items.empty())
				add_default_lower_third();
		}
	} else {
		if (g_items.empty())
			add_default_lower_third();
	}
}

} // namespace smart_lt
