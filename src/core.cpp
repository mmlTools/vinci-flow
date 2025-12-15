#define LOG_TAG "[" PLUGIN_NAME "][core]"
#include "core.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <tuple>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

namespace smart_lt {

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static std::vector<LowerThirdConfig> g_items;
static std::string g_output_dir;      // user resources folder
static std::string g_index_html_path; // absolute
static std::string g_global_cfg_path; // module config json (stores last output_dir)
static uint64_t g_rev = 1;            // bumps only on major changes

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static std::string module_config_path_cached()
{
	if (!g_global_cfg_path.empty())
		return g_global_cfg_path;

	char *p = obs_module_config_path("config.json");
	if (!p) {
		LOGW("obs_module_config_path returned null for config.json");
		return {};
	}

	g_global_cfg_path = p;
	bfree(p);

	LOGD("Global config path: '%s'", g_global_cfg_path.c_str());
	return g_global_cfg_path;
}

static void update_index_path_from_output_dir()
{
	g_index_html_path.clear();
	if (g_output_dir.empty())
		return;

	std::string p = g_output_dir;
	const char last = p.back();
	if (last != '/' && last != '\\')
		p += '/';

	p += "smart-lower-thirds.html";
	g_index_html_path = p;
}

static bool qt_write_file_atomic(const QString &path, const QByteArray &bytes, bool allowMkPath)
{
	const QFileInfo fi(path);
	if (allowMkPath) {
		QDir().mkpath(fi.absolutePath());
	} else {
		if (!fi.dir().exists())
			return false;
	}

	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;

	if (f.write(bytes) != bytes.size())
		return false;

	return f.commit();
}

static void save_global_config()
{
	const std::string pathS = module_config_path_cached();
	if (pathS.empty())
		return;

	QJsonObject root;
	root["output_dir"] = QString::fromStdString(g_output_dir);

	const QJsonDocument doc(root);
	const QByteArray bytes = doc.toJson(QJsonDocument::Compact);

	const QString path = QString::fromStdString(pathS);
	if (!qt_write_file_atomic(path, bytes, /*allowMkPath*/ true)) {
		LOGW("Failed to save global config '%s'", path.toUtf8().constData());
		return;
	}

	LOGD("Saved global config '%s' (output_dir='%s')", path.toUtf8().constData(), g_output_dir.c_str());
}

static void load_global_config()
{
	const std::string pathS = module_config_path_cached();
	if (pathS.empty())
		return;

	const QString path = QString::fromStdString(pathS);
	QFile f(path);
	if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
		LOGD("No global config at '%s' (first run ok)", path.toUtf8().constData());
		return;
	}

	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject())
		return;

	const QJsonObject root = doc.object();
	const QString out = root.value("output_dir").toString();
	if (!out.isEmpty())
		g_output_dir = out.toStdString();

	update_index_path_from_output_dir();

	if (!g_output_dir.empty())
		LOGI("Loaded output_dir: '%s'", g_output_dir.c_str());
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
	// "#AARRGGBB" -> rgba(r,g,b,a)
	if (stored.size() == 9 && stored[0] == '#') {
		auto hexByte = [&](int idx) -> int {
			char buf[3] = {stored[idx], stored[idx + 1], 0};
			char *end = nullptr;
			const long v = strtol(buf, &end, 16);
			if (!end || *end)
				return 0;
			return (int)v;
		};

		const int a = hexByte(1);
		const int r = hexByte(3);
		const int g = hexByte(5);
		const int b = hexByte(7);

		char out[64];
		std::snprintf(out, sizeof(out), "rgba(%d,%d,%d,%.3f)", r, g, b, (double)a / 255.0);
		return out;
	}

	return stored; // "#RRGGBB"
}

// -----------------------------------------------------------------------------
// Public paths
// -----------------------------------------------------------------------------

void set_output_dir(const std::string &dir)
{
	g_output_dir = dir;
	update_index_path_from_output_dir();
	save_global_config();
}

bool has_output_dir()
{
	return !g_output_dir.empty() && !g_index_html_path.empty();
}

const std::string &output_dir()
{
	return g_output_dir;
}

const std::string &index_html_path()
{
	return g_index_html_path;
}

std::string state_json_path()
{
	if (!has_output_dir())
		return {};

	std::string p = g_output_dir;
	const char last = p.back();
	if (last != '/' && last != '\\')
		p += '/';
	p += "smart-lower-thirds-state.json";
	return p;
}

// -----------------------------------------------------------------------------
// Default config
// -----------------------------------------------------------------------------

static std::string make_new_id()
{
	size_t idx = g_items.size() + 1;
	for (;;) {
		const std::string candidate = "lower-third-" + std::to_string(idx);

		const bool exists = std::any_of(g_items.begin(), g_items.end(),
						[&](const LowerThirdConfig &c) { return c.id == candidate; });

		if (!exists)
			return candidate;

		++idx;
	}
}

static LowerThirdConfig make_default_cfg()
{
	LowerThirdConfig cfg;
	cfg.id = make_new_id();

	cfg.title = "Your Name";
	cfg.subtitle = "Role / Topic";

	cfg.anim_in = "animate__fadeInUp";
	cfg.anim_out = "animate__fadeOutDown";
	cfg.custom_anim_in.clear();
	cfg.custom_anim_out.clear();

	cfg.lt_position = "lt-pos-bottom-left";
	cfg.font_family = "Inter";

	cfg.bg_color = "#111827";
	cfg.text_color = "#F9FAFB";

	cfg.hotkey.clear();
	cfg.visible = false;
	cfg.profile_picture.clear();

	cfg.html_template =
		"<li id=\"{{ID}}\" class=\"lower-third {{LT_POSITION}} animate__animated\" style=\"opacity:0;\">\n"
		"  <div class=\"slt-card\">\n"
		"    <div class=\"slt-accent\"></div>\n"
		"    <div class=\"slt-body\">\n"
		"      <div class=\"slt-avatar-wrap\" data-has-avatar=\"{{HAS_AVATAR}}\">\n"
		"        <img class=\"slt-avatar\" src=\"{{PROFILE_PICTURE}}\" alt=\"\" />\n"
		"      </div>\n"
		"      <div class=\"slt-text\">\n"
		"        <div class=\"slt-title\">{{TITLE}}</div>\n"
		"        <div class=\"slt-subtitle\">{{SUBTITLE}}</div>\n"
		"      </div>\n"
		"    </div>\n"
		"  </div>\n"
		"</li>\n";

	cfg.css_template =
		"#{{ID}} .slt-card{position:relative;display:inline-block;border-radius:14px;overflow:hidden;"
		"box-shadow:0 14px 40px rgba(0,0,0,0.35);transform:translateZ(0);} \n"
		"#{{ID}} .slt-accent{position:absolute;inset:0 auto 0 0;width:6px;"
		"background:linear-gradient(180deg,rgba(59,130,246,1),rgba(34,197,94,1));opacity:.95;} \n"
		"#{{ID}} .slt-body{display:inline-flex;align-items:center;gap:12px;padding:14px 18px 14px 14px;"
		"background:rgba(0,0,0,0.25);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);"
		"border:1px solid rgba(255,255,255,0.16);border-left:none;} \n"
		"#{{ID}} .slt-body{background-color:{{BG_COLOR}};color:{{TEXT_COLOR}};} \n"
		"#{{ID}} .slt-avatar-wrap{width:44px;height:44px;border-radius:999px;overflow:hidden;"
		"border:2px solid rgba(255,255,255,0.22);flex:0 0 auto;} \n"
		"#{{ID}} .slt-avatar-wrap[data-has-avatar=\"0\"]{display:none;} \n"
		"#{{ID}} .slt-avatar{width:100%;height:100%;object-fit:cover;display:block;} \n"
		"#{{ID}} .slt-text{display:flex;flex-direction:column;gap:2px;} \n"
		"#{{ID}} .slt-title{font-family:{{FONT_FAMILY}},system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
		"font-size:28px;font-weight:800;letter-spacing:.2px;line-height:1.05;"
		"text-shadow:0 2px 10px rgba(0,0,0,0.28);} \n"
		"#{{ID}} .slt-subtitle{font-family:{{FONT_FAMILY}},system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
		"font-size:16px;font-weight:500;opacity:.9;letter-spacing:.25px;} \n";

	return cfg;
}

// -----------------------------------------------------------------------------
// State access
// -----------------------------------------------------------------------------

std::vector<LowerThirdConfig> &all()
{
	return g_items;
}

LowerThirdConfig *get_by_id(const std::string &id)
{
	for (auto &it : g_items) {
		if (it.id == id)
			return &it;
	}
	return nullptr;
}

// -----------------------------------------------------------------------------
// JSON persistence (rev is authoritative here)
// -----------------------------------------------------------------------------

static QJsonObject cfg_to_json(const LowerThirdConfig &cfg)
{
	QJsonObject o;
	o["id"] = QString::fromStdString(cfg.id);
	o["title"] = QString::fromStdString(cfg.title);
	o["subtitle"] = QString::fromStdString(cfg.subtitle);
	o["anim_in"] = QString::fromStdString(cfg.anim_in);
	o["anim_out"] = QString::fromStdString(cfg.anim_out);
	o["custom_anim_in"] = QString::fromStdString(cfg.custom_anim_in);
	o["custom_anim_out"] = QString::fromStdString(cfg.custom_anim_out);
	o["font_family"] = QString::fromStdString(cfg.font_family);
	o["lt_position"] = QString::fromStdString(cfg.lt_position);
	o["bg_color"] = QString::fromStdString(cfg.bg_color);
	o["text_color"] = QString::fromStdString(cfg.text_color);
	o["html_template"] = QString::fromStdString(cfg.html_template);
	o["css_template"] = QString::fromStdString(cfg.css_template);
	o["visible"] = cfg.visible;
	o["hotkey"] = QString::fromStdString(cfg.hotkey);
	o["profile_picture"] = QString::fromStdString(cfg.profile_picture);
	return o;
}

bool save_state_json()
{
	if (!has_output_dir())
		return false;

	const QString path = QString::fromStdString(state_json_path());
	if (path.isEmpty())
		return false;

	const QFileInfo fi(path);
	if (!fi.dir().exists()) {
		LOGW("Output directory does not exist: '%s'", fi.dir().absolutePath().toUtf8().constData());
		return false;
	}

	QJsonObject root;
	root["rev"] = static_cast<qint64>(g_rev);
	root["saved_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	QJsonArray arr;
	for (const auto &cfg : g_items)
		arr.append(cfg_to_json(cfg));
	root["items"] = arr;

	const QJsonDocument doc(root);
	const QByteArray bytes = doc.toJson(QJsonDocument::Compact);

	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOGW("Failed to open state JSON '%s' (%s)", path.toUtf8().constData(),
		     f.errorString().toUtf8().constData());
		return false;
	}

	if (f.write(bytes) != bytes.size()) {
		LOGW("Failed to write state JSON '%s' (%s)", path.toUtf8().constData(),
		     f.errorString().toUtf8().constData());
		return false;
	}

	if (!f.commit()) {
		LOGW("Failed to commit state JSON '%s' (%s)", path.toUtf8().constData(),
		     f.errorString().toUtf8().constData());
		return false;
	}

	LOGD("Saved '%s' (rev=%llu)", path.toUtf8().constData(), (unsigned long long)g_rev);
	return true;
}

bool load_state_json()
{
	if (!has_output_dir())
		return false;

	const QString path = QString::fromStdString(state_json_path());
	if (path.isEmpty())
		return false;

	QFile f(path);
	if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
		LOGD("No state JSON at '%s' (ok if folder is empty)", path.toUtf8().constData());
		return false;
	}

	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		LOGW("Invalid JSON in '%s'", path.toUtf8().constData());
		return false;
	}

	const QJsonObject root = doc.object();

	g_rev = static_cast<uint64_t>(root.value("rev").toVariant().toLongLong());
	if (g_rev == 0)
		g_rev = 1;

	g_items.clear();

	const QJsonValue itemsVal = root.value("items");
	if (itemsVal.isArray()) {
		const QJsonArray itemsArr = itemsVal.toArray();
		g_items.reserve(itemsArr.size());

		for (const QJsonValue &v : itemsArr) {
			if (!v.isObject())
				continue;

			const QJsonObject o = v.toObject();
			LowerThirdConfig cfg;

			cfg.id = o.value("id").toString().toStdString();
			cfg.title = o.value("title").toString().toStdString();
			cfg.subtitle = o.value("subtitle").toString().toStdString();
			cfg.anim_in = o.value("anim_in").toString().toStdString();
			cfg.anim_out = o.value("anim_out").toString().toStdString();
			cfg.custom_anim_in = o.value("custom_anim_in").toString().toStdString();
			cfg.custom_anim_out = o.value("custom_anim_out").toString().toStdString();
			cfg.font_family = o.value("font_family").toString().toStdString();
			cfg.lt_position = o.value("lt_position").toString().toStdString();
			cfg.bg_color = o.value("bg_color").toString().toStdString();
			cfg.text_color = o.value("text_color").toString().toStdString();
			cfg.html_template = o.value("html_template").toString().toStdString();
			cfg.css_template = o.value("css_template").toString().toStdString();
			cfg.visible = o.value("visible").toBool(false);
			cfg.hotkey = o.value("hotkey").toString().toStdString();
			cfg.profile_picture = o.value("profile_picture").toString().toStdString();

			// defaults safety
			if (cfg.lt_position.empty())
				cfg.lt_position = "lt-pos-bottom-left";
			if (cfg.anim_in.empty())
				cfg.anim_in = "animate__fadeInUp";
			if (cfg.anim_out.empty())
				cfg.anim_out = "animate__fadeOutDown";
			if (cfg.font_family.empty())
				cfg.font_family = "Inter";
			if (cfg.bg_color.empty())
				cfg.bg_color = "#111827";
			if (cfg.text_color.empty())
				cfg.text_color = "#F9FAFB";

			if (!cfg.id.empty())
				g_items.push_back(std::move(cfg));
		}
	}

	LOGI("Loaded '%s' (%zu items, rev=%llu)", path.toUtf8().constData(), g_items.size(), (unsigned long long)g_rev);
	return true;
}

// -----------------------------------------------------------------------------
// Browser source repoint (managed only)
// -----------------------------------------------------------------------------

static bool slt_is_browser_source(obs_source_t *src)
{
	if (!src)
		return false;

	const char *id = obs_source_get_id(src);
	return id && std::strcmp(id, "browser_source") == 0;
}

static bool slt_is_managed(obs_data_t *settings)
{
	return settings && obs_data_get_bool(settings, "smart_lt_managed");
}

static void slt_reload_browser_source(obs_source_t *src, obs_data_t *settings)
{
	const bool shutdown = obs_data_get_bool(settings, "shutdown");
	obs_data_set_bool(settings, "shutdown", !shutdown);
	obs_source_update(src, settings);

	obs_data_set_bool(settings, "shutdown", shutdown);
	obs_source_update(src, settings);
}

void repoint_managed_browser_sources(bool reload)
{
	if (!has_output_dir())
		return;

	const std::string &htmlPath = index_html_path();
	if (htmlPath.empty())
		return;

	size_t visited = 0, updated = 0;

	auto cb = [](void *param, obs_source_t *src) -> bool {
		auto *ctx = static_cast<std::tuple<const char *, bool, size_t *, size_t *> *>(param);
		const char *desired = std::get<0>(*ctx);
		const bool doReload = std::get<1>(*ctx);
		size_t *visitedPtr = std::get<2>(*ctx);
		size_t *updatedPtr = std::get<3>(*ctx);

		if (!src || !slt_is_browser_source(src))
			return true;

		(*visitedPtr)++;

		obs_data_t *settings = obs_source_get_settings(src);
		if (!settings)
			return true;

		if (!slt_is_managed(settings)) {
			obs_data_release(settings);
			return true;
		}

		obs_data_set_bool(settings, "is_local_file", true);

		const char *cur = obs_data_get_string(settings, "local_file");
		const bool differs = (!cur || std::strcmp(cur, desired) != 0);

		if (differs) {
			obs_data_set_string(settings, "local_file", desired);
			obs_source_update(src, settings);
			(*updatedPtr)++;
		}

		if (doReload)
			slt_reload_browser_source(src, settings);

		obs_data_release(settings);
		return true;
	};

	std::tuple<const char *, bool, size_t *, size_t *> ctx{htmlPath.c_str(), reload, &visited, &updated};
	obs_enum_sources(cb, &ctx);

	LOGI("Repoint managed browser sources: updated=%zu visited=%zu html='%s'", updated, visited, htmlPath.c_str());
}

// -----------------------------------------------------------------------------
// HTML writer (embeds g_rev as HTML_REV; reload happens when json.rev > HTML_REV)
// -----------------------------------------------------------------------------

bool write_index_html()
{
	if (!has_output_dir()) {
		LOGW("Output directory not set; skipping HTML generation");
		return false;
	}

	const QString htmlPath = QString::fromStdString(g_index_html_path);
	const QFileInfo fi(htmlPath);
	if (!fi.dir().exists()) {
		LOGW("Output directory does not exist: '%s'", fi.dir().absolutePath().toUtf8().constData());
		return false;
	}

	std::string html;
	html.reserve(26000);

	html += "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\" />\n";
	html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n";
	html += "<title>Smart Lower Thirds</title>\n";
	html += "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/animate.css/4.1.1/animate.min.css\" />\n";
	html += "<style>\n";
	html += "html,body{margin:0;padding:0;background:transparent;overflow:hidden;}\n";
	html += "ul#lower-thirds-root{list-style:none;margin:0;padding:0;}\n";
	html += ".lower-third{position:absolute;opacity:0;pointer-events:none;}\n";
	html += ".lt-pos-bottom-left{left:5%;bottom:5%;top:auto;right:auto;transform:translate(0,0);}\n";
	html += ".lt-pos-bottom-right{right:5%;bottom:5%;left:auto;top:auto;transform:translate(0,0);}\n";
	html += ".lt-pos-top-left{left:5%;top:5%;right:auto;bottom:auto;transform:translate(0,0);}\n";
	html += ".lt-pos-top-right{right:5%;top:5%;left:auto;bottom:auto;transform:translate(0,0);}\n";
	html += ".lt-pos-center{top:50%;left:50%;right:auto;bottom:auto;transform:translate(-50%,-50%);}\n";

	for (const auto &cfg : g_items) {
		std::string css = cfg.css_template;
		css = replace_all(css, "{{ID}}", cfg.id);
		css = replace_all(css, "{{FONT_FAMILY}}", cfg.font_family);
		css = replace_all(css, "{{BG_COLOR}}", css_from_stored_color(cfg.bg_color));
		css = replace_all(css, "{{TEXT_COLOR}}", css_from_stored_color(cfg.text_color));
		html += css;
		html += "\n";
	}

	html += "</style>\n</head>\n<body>\n<ul id=\"lower-thirds-root\">\n";

	for (const auto &cfg : g_items) {
		std::string item = cfg.html_template;

		const bool hasAvatar = !cfg.profile_picture.empty();
		const std::string hasAvatarStr = hasAvatar ? "1" : "0";

		item = replace_all(item, "{{ID}}", cfg.id);
		item = replace_all(item, "{{TITLE}}", cfg.title);
		item = replace_all(item, "{{SUBTITLE}}", cfg.subtitle);
		item = replace_all(item, "{{PROFILE_PICTURE}}", cfg.profile_picture);
		item = replace_all(item, "{{HAS_AVATAR}}", hasAvatarStr);
		item = replace_all(item, "{{LT_POSITION}}", cfg.lt_position);

		html += item;
		html += "\n";
	}

	html += "</ul>\n";

	// Build list of known animate.css classes to remove
	std::string jsAllAnims = "  const ALL_ANIMS=[";
	bool first = true;
	auto append = [&](const char *cls) {
		if (!cls || !*cls)
			return;
		if (!first)
			jsAllAnims += ",";
		jsAllAnims += "'";
		jsAllAnims += cls;
		jsAllAnims += "'";
		first = false;
	};
	for (const auto &opt : AnimInOptions)
		if (std::string(opt.value) != "custom")
			append(opt.value);
	for (const auto &opt : AnimOutOptions)
		if (std::string(opt.value) != "custom")
			append(opt.value);
	jsAllAnims += "];\n";

	html += "<script>\n(function(){\n";
	html += "  const HTML_REV=" + std::to_string(g_rev) + ";\n";
	html += "  const nodes=[...document.querySelectorAll('.lower-third')];\n";
	html += "  const byId={}; nodes.forEach(li=>{ if(li.id) byId[li.id]=li; });\n";
	html += jsAllAnims;
	html += "  nodes.forEach(li=>{ li.style.opacity='0'; li.style.pointerEvents='none'; });\n";
	html += "  const lastVisible=new Set();\n";
	html += "  function clearAnim(li){ ALL_ANIMS.forEach(a=>li.classList.remove(a)); }\n";
	html += "  function effectiveIn(it){ return (it.anim_in==='custom') ? (it.custom_anim_in||'') : (it.anim_in||''); }\n";
	html += "  function effectiveOut(it){ return (it.anim_out==='custom') ? (it.custom_anim_out||'') : (it.anim_out||''); }\n";
	html += "  function applyState(items){\n";
	html += "    const visible=new Set();\n";
	html += "    const anim={};\n";
	html += "    for(const it of items){ if(!it||!it.id) continue; anim[it.id]={in:effectiveIn(it), out:effectiveOut(it)}; if(it.visible) visible.add(it.id); }\n";
	html += "    const toHide=[]; const toShow=[];\n";
	html += "    lastVisible.forEach(id=>{ if(!visible.has(id)) toHide.push(id); });\n";
	html += "    visible.forEach(id=>{ if(!lastVisible.has(id)) toShow.push(id); });\n";
	html += "    for(const id of toHide){\n";
	html += "      const li=byId[id]; if(!li) continue;\n";
	html += "      const out=(anim[id]&&anim[id].out) ? anim[id].out : 'animate__fadeOut';\n";
	html += "      clearAnim(li); li.style.pointerEvents='none'; li.style.opacity='1';\n";
	html += "      if(out) li.classList.add(out);\n";
	html += "      const handler=()=>{ li.removeEventListener('animationend', handler); clearAnim(li); li.style.opacity='0'; };\n";
	html += "      li.addEventListener('animationend', handler);\n";
	html += "    }\n";
	html += "    for(const id of toShow){\n";
	html += "      const li=byId[id]; if(!li) continue;\n";
	html += "      const inn=(anim[id]&&anim[id].in) ? anim[id].in : 'animate__fadeIn';\n";
	html += "      clearAnim(li); li.style.opacity='1'; li.style.pointerEvents='auto';\n";
	html += "      if(inn) li.classList.add(inn);\n";
	html += "    }\n";
	html += "    lastVisible.clear(); visible.forEach(id=>lastVisible.add(id));\n";
	html += "  }\n";
	html += "  async function tick(){\n";
	html += "    try{\n";
	html += "      const res=await fetch('smart-lower-thirds-state.json?ts='+Date.now(), {cache:'no-store'});\n";
	html += "      if(!res.ok) return;\n";
	html += "      const data=await res.json();\n";
	html += "      const srvRev=Number(data.rev||0);\n";
	html += "      if(srvRev>HTML_REV){ location.reload(); return; }\n";
	html += "      applyState(data.items||[]);\n";
	html += "    }catch(e){}\n";
	html += "  }\n";
	html += "  tick(); setInterval(tick, 500);\n";
	html += "})();\n</script>\n";

	html += "</body>\n</html>\n";

	QSaveFile f(htmlPath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOGE("Failed to open '%s' for writing (%s)", htmlPath.toUtf8().constData(),
		     f.errorString().toUtf8().constData());
		return false;
	}

	const QByteArray bytes = QByteArray::fromStdString(html);
	if (f.write(bytes) != bytes.size()) {
		LOGE("Failed to write HTML '%s' (%s)", htmlPath.toUtf8().constData(),
		     f.errorString().toUtf8().constData());
		return false;
	}

	if (!f.commit()) {
		LOGE("Failed to commit HTML '%s' (%s)", htmlPath.toUtf8().constData(),
		     f.errorString().toUtf8().constData());
		return false;
	}

	LOGI("Wrote '%s' (rev=%llu)", htmlPath.toUtf8().constData(), (unsigned long long)g_rev);
	return true;
}

// -----------------------------------------------------------------------------
// Ensure artifacts exist (no rev bump)
// -----------------------------------------------------------------------------

bool ensure_output_artifacts_exist()
{
	if (!has_output_dir())
		return false;

	const QString jsonPath = QString::fromStdString(state_json_path());
	const QString htmlPath = QString::fromStdString(index_html_path());

	const QFileInfo jfi(jsonPath);
	if (!jfi.dir().exists())
		return false;

	bool changed = false;

	if (!QFileInfo::exists(jsonPath)) {
		if (g_items.empty())
			g_items.push_back(make_default_cfg());
		if (g_rev == 0)
			g_rev = 1;

		if (save_state_json())
			changed = true;
	}

	if (!QFileInfo::exists(htmlPath)) {
		if (write_index_html())
			changed = true;
	}

	return changed;
}

// -----------------------------------------------------------------------------
// Apply
// -----------------------------------------------------------------------------

void apply_changes(ApplyMode mode)
{
	if (!has_output_dir())
		return;

	if (mode == ApplyMode::JsonOnly) {
		save_state_json();
		return;
	}

	// Major:
	// - rev++
	// - write html with embedded rev
	// - save json with new rev
	// - reload managed browser sources
	g_rev++;
	write_index_html();
	save_state_json();
	repoint_managed_browser_sources(/*reload*/ true);
}

// -----------------------------------------------------------------------------
// CRUD (major)
// -----------------------------------------------------------------------------

std::string add_default_lower_third()
{
	LowerThirdConfig cfg = make_default_cfg();
	g_items.push_back(cfg);
	apply_changes(ApplyMode::HtmlAndJsonRev);
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
			apply_changes(ApplyMode::HtmlAndJsonRev);
			return copy.id;
		}
	}
	return {};
}

void remove_lower_third(const std::string &id)
{
	auto it = std::find_if(g_items.begin(), g_items.end(), [&](const LowerThirdConfig &c) { return c.id == id; });
	if (it != g_items.end()) {
		g_items.erase(it);
		apply_changes(ApplyMode::HtmlAndJsonRev);
	}
}

// -----------------------------------------------------------------------------
// Visibility (minor)
// -----------------------------------------------------------------------------

void toggle_active(const std::string &id, bool hideOthers)
{
	if (g_items.empty())
		return;

	if (!hideOthers) {
		for (auto &it : g_items) {
			if (it.id == id) {
				it.visible = !it.visible;
				break;
			}
		}
		apply_changes(ApplyMode::JsonOnly);
		return;
	}

	bool thisVisible = false;
	bool thisIsOnlyVisible = true;

	for (const auto &it : g_items) {
		if (!it.visible)
			continue;

		if (it.id == id)
			thisVisible = true;
		else
			thisIsOnlyVisible = false;
	}

	if (thisVisible && thisIsOnlyVisible) {
		for (auto &it : g_items)
			it.visible = false;
	} else {
		for (auto &it : g_items)
			it.visible = (it.id == id);
	}

	apply_changes(ApplyMode::JsonOnly);
}

void set_active_exact(const std::string &id)
{
	if (g_items.empty())
		return;

	if (id.empty()) {
		for (auto &it : g_items)
			it.visible = false;
		apply_changes(ApplyMode::JsonOnly);
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

	apply_changes(ApplyMode::JsonOnly);
}

// -----------------------------------------------------------------------------
// Output dir switching
// -----------------------------------------------------------------------------

bool set_output_dir_and_load(const std::string &dir)
{
	if (dir.empty())
		return false;

	const bool sameDir = (dir == g_output_dir && has_output_dir());

	set_output_dir(dir);

	const QString qDir = QString::fromStdString(g_output_dir);
	if (!QDir(qDir).exists()) {
		LOGW("Selected folder does not exist: '%s'", g_output_dir.c_str());
		if (g_items.empty())
			g_items.push_back(make_default_cfg());
		return false;
	}

	// If JSON exists: load it. Otherwise seed defaults (without bumping rev).
	const QString jsonPath = QString::fromStdString(state_json_path());
	if (QFileInfo::exists(jsonPath)) {
		load_state_json();
	} else {
		g_items.clear();
		g_items.push_back(make_default_cfg());
		g_rev = 1;
		save_state_json();
	}

	// Ensure HTML exists (do not bump rev)
	ensure_output_artifacts_exist();

	// Repoint managed sources. Reload only if the dir actually changed.
	repoint_managed_browser_sources(/*reload*/ !sameDir);

	LOGI("Folder active: '%s' (rev=%llu, items=%zu)", g_output_dir.c_str(), (unsigned long long)g_rev,
	     g_items.size());

	return true;
}

// -----------------------------------------------------------------------------
// init (on OBS load)
// -----------------------------------------------------------------------------

void init_from_disk()
{
	load_global_config();

	if (!g_output_dir.empty()) {
		set_output_dir_and_load(g_output_dir);
	} else {
		if (g_items.empty())
			g_items.push_back(make_default_cfg());
		if (g_rev == 0)
			g_rev = 1;
	}
}

} // namespace smart_lt
