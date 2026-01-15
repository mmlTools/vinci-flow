// core.cpp
#define LOG_TAG "[" PLUGIN_NAME "][core]"
#include "core.hpp"

#include <algorithm>
#include <sstream>
#include <random>
#include <cctype>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

#include <chrono>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <obs.h>
#include <obs-module.h>
#include <filesystem>
#include <system_error>

namespace smart_lt {

// -------------------------
// Globals
// -------------------------
static std::string g_output_dir;
static std::string g_target_browser_source;
static int g_target_browser_width = sltBrowserWidth;
static int g_target_browser_height = sltBrowserHeight;
static bool g_dock_exclusive_mode = false;
static std::vector<lower_third_cfg> g_items;
static std::vector<carousel_cfg> g_carousels;
static std::vector<std::string> g_visible;
static std::string g_last_html_path;

// -------------------------
// Event bus impl
// -------------------------
struct listener {
	uint64_t token = 0;
	core_event_cb cb = nullptr;
	void *user = nullptr;
};

static std::mutex g_evt_mx;
static std::vector<listener> g_listeners;
static uint64_t g_next_token = 1;

static void emit_event(const core_event &ev)
{
	std::vector<listener> copy;
	{
		std::lock_guard<std::mutex> lk(g_evt_mx);
		copy = g_listeners;
	}
	for (const auto &l : copy) {
		if (l.cb)
			l.cb(ev, l.user);
	}
}

uint64_t add_event_listener(core_event_cb cb, void *user)
{
	if (!cb)
		return 0;

	std::lock_guard<std::mutex> lk(g_evt_mx);
	const uint64_t t = g_next_token++;
	g_listeners.push_back(listener{t, cb, user});
	return t;
}

void remove_event_listener(uint64_t token)
{
	if (token == 0)
		return;

	std::lock_guard<std::mutex> lk(g_evt_mx);
	g_listeners.erase(
		std::remove_if(g_listeners.begin(), g_listeners.end(),
			       [&](const listener &l) { return l.token == token; }),
		g_listeners.end());
}

// -------------------------
// Helpers
// -------------------------
static std::string join_path(const std::string &a, const std::string &b)
{
	QDir d(QString::fromStdString(a));
	return d.filePath(QString::fromStdString(b)).toStdString();
}

static bool write_text_file(const std::string &path, const std::string &data)
{
	QFile f(QString::fromStdString(path));
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOGW("Failed opening '%s' for write (err=%d '%s')", path.c_str(), (int)f.error(),
		     f.errorString().toUtf8().constData());
		return false;
	}
	const qint64 written = f.write(data.data(), (qint64)data.size());
	if (written != (qint64)data.size()) {
		LOGW("Short write for '%s' (%lld/%lld)", path.c_str(), (long long)written,
		     (long long)data.size());
		f.close();
		return false;
	}
	f.flush();
	f.close();
	return true;
}

static std::string read_text_file(const std::string &path)
{
	QFile f(QString::fromStdString(path));
	if (!f.open(QIODevice::ReadOnly))
		return {};
	const QByteArray b = f.readAll();
	f.close();
	return std::string(b.constData(), (size_t)b.size());
}

static void ensure_dir(const std::string &dir)
{
	QDir().mkpath(QString::fromStdString(dir));
}

static std::string sanitize_id(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
		    c == '-') {
			out.push_back(c);
		}
	}
	if (out.empty())
		out = "lt_" + now_timestamp_string();
	return out;
}

static std::string replace_all(std::string s, const std::string &from, const std::string &to)
{
	if (from.empty())
		return s;
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos) {
		s.replace(pos, from.size(), to);
		pos += to.size();
	}
	return s;
}


// -------------------------
// CSS keyframes extraction / dedupe
// -------------------------
static bool is_ident_char(char c)
{
	return std::isalnum((unsigned char)c) || c == '_' || c == '-';
}

static std::string normalize_ws_no_space(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		if (!std::isspace((unsigned char)c))
			out.push_back(c);
	}
	return out;
}

// Replace occurrences of identifier 'from' with 'to' when it appears as a whole identifier.
// This is used to rewrite animation-name references after resolving keyframe name conflicts.
static std::string replace_whole_ident(std::string s, const std::string &from, const std::string &to)
{
	if (from.empty())
		return s;

	auto is_boundary = [](char c) { return !is_ident_char(c); };

	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos) {
		const bool left_ok = (pos == 0) || is_boundary(s[pos - 1]);
		const bool right_ok = (pos + from.size() >= s.size()) || is_boundary(s[pos + from.size()]);
		if (left_ok && right_ok) {
			s.replace(pos, from.size(), to);
			pos += to.size();
		} else {
			pos += from.size();
		}
	}
	return s;
}

struct extracted_keyframes {
	std::string at_rule; // "@keyframes" or "@-webkit-keyframes"
	std::string name;    // parsed name (may be empty if malformed)
	std::string block;   // full block text
	std::string norm;    // normalized for dedupe
};

static void extract_keyframes_blocks(std::string &css, std::vector<extracted_keyframes> &out)
{
	auto find_next = [&](size_t start) -> std::pair<size_t, std::string> {
		const size_t p1 = css.find("@keyframes", start);
		const size_t p2 = css.find("@-webkit-keyframes", start);
		if (p1 == std::string::npos && p2 == std::string::npos)
			return {std::string::npos, {}};
		if (p2 == std::string::npos || (p1 != std::string::npos && p1 < p2))
			return {p1, "@keyframes"};
		return {p2, "@-webkit-keyframes"};
	};

	size_t cur = 0;
	while (true) {
		auto [pos, atr] = find_next(cur);
		if (pos == std::string::npos)
			break;

		// Parse name: after at-rule token, skip whitespace, read identifier
		size_t nameStart = pos + atr.size();
		while (nameStart < css.size() && std::isspace((unsigned char)css[nameStart]))
			nameStart++;

		size_t nameEnd = nameStart;
		while (nameEnd < css.size() && is_ident_char(css[nameEnd]))
			nameEnd++;

		std::string name;
		if (nameEnd > nameStart)
			name = css.substr(nameStart, nameEnd - nameStart);

		// Find opening brace
		size_t braceOpen = css.find('{', nameEnd);
		if (braceOpen == std::string::npos) {
			cur = nameEnd;
			continue;
		}

		// Match braces
		int depth = 0;
		size_t i = braceOpen;
		for (; i < css.size(); i++) {
			if (css[i] == '{')
				depth++;
			else if (css[i] == '}') {
				depth--;
				if (depth == 0) {
					const size_t endPos = i + 1;
					const std::string block = css.substr(pos, endPos - pos);

					extracted_keyframes kf;
					kf.at_rule = atr;
					kf.name = name;
					kf.block = block;
					kf.norm = normalize_ws_no_space(block);

					out.push_back(std::move(kf));

					// Remove from css (leave a newline to avoid accidental token joining)
					css.replace(pos, endPos - pos, "\n");
					cur = pos;
					break;
				}
			}
		}

		if (depth != 0) {
			// Unbalanced braces; abort to avoid infinite loop
			break;
		}
	}
}


static void delete_old_lt_html_keep(const std::string &keepAbsPath)
{
	if (!has_output_dir())
		return;

	QDir d(QString::fromStdString(output_dir()));
	const QFileInfo keepFi(QString::fromStdString(keepAbsPath));
	const QString keepName = keepFi.fileName();

	const QStringList list = d.entryList(QStringList() << "lt-*.html", QDir::Files, QDir::Time);
	for (const QString &fn : list) {
		if (!keepName.isEmpty() && fn == keepName)
			continue;
		d.remove(fn);
	}
}

static bool file_exists(const std::string &path)
{
	return QFileInfo(QString::fromStdString(path)).exists();
}

// -------------------------
// OBS module config.json
// -------------------------
static std::string module_config_path_cached()
{
	static std::string cached;
	static bool inited = false;
	if (inited)
		return cached;

	inited = true;

	char *p = obs_module_config_path("config.json");
	if (!p)
		return cached;

	cached = p;
	bfree(p);
	return cached;
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

	const QString out = root.value("output_dir").toString().trimmed();
	if (!out.isEmpty()) {
		g_output_dir = out.toStdString();
		LOGI("Loaded output_dir: '%s'", g_output_dir.c_str());
	}

	const QString tgt = root.value("target_browser_source").toString().trimmed();
	if (!tgt.isEmpty()) {
		g_target_browser_source = tgt.toStdString();
		LOGI("Loaded target_browser_source: '%s'", g_target_browser_source.c_str());
	}

	const int w = root.value("target_browser_width").toInt(sltBrowserWidth);
	const int h = root.value("target_browser_height").toInt(sltBrowserHeight);
	if (w > 0)
		g_target_browser_width = w;
	if (h > 0)
		g_target_browser_height = h;
	g_dock_exclusive_mode = root.value("dock_exclusive_mode").toBool(false);
}

bool save_global_config()
{
	const std::string pathS = module_config_path_cached();
	if (pathS.empty())
		return false;

	QFileInfo fi(QString::fromStdString(pathS));
	QDir().mkpath(fi.absolutePath());

	QJsonObject root;
	root["output_dir"] = QString::fromStdString(g_output_dir);
	root["target_browser_source"] = QString::fromStdString(g_target_browser_source);
	root["target_browser_width"] = g_target_browser_width;
	root["target_browser_height"] = g_target_browser_height;
	root["dock_exclusive_mode"] = g_dock_exclusive_mode;

	const QJsonDocument doc(root);
	return write_text_file(pathS, doc.toJson(QJsonDocument::Compact).toStdString());
}

static std::string find_latest_lt_html()
{
	if (!has_output_dir())
		return {};

	QDir d(QString::fromStdString(output_dir()));
	const QStringList list = d.entryList(QStringList() << "lt-*.html", QDir::Files, QDir::Time);
	if (list.isEmpty())
		return {};
	return d.filePath(list.first()).toStdString();
}

// CSS/JS use stable filenames so external references remain constant, but we
// cache-bust them via querystring (?v=<timestamp>) inside each new HTML bundle.
static std::string bundle_styles_name(const std::string &) { return "lt.css"; }
static std::string bundle_scripts_name(const std::string &) { return "lt.js"; }
static std::string bundle_html_name(const std::string &ts) { return "lt-" + ts + ".html"; }

static std::string bundle_styles_path(const std::string &ts)
{
	return has_output_dir() ? join_path(output_dir(), bundle_styles_name(ts)) : std::string();
}

static std::string bundle_scripts_path(const std::string &ts)
{
	return has_output_dir() ? join_path(output_dir(), bundle_scripts_name(ts)) : std::string();
}

static std::string bundle_html_path(const std::string &ts)
{
	return has_output_dir() ? join_path(output_dir(), bundle_html_name(ts)) : std::string();
}

static void cleanup_old_bundles(int keep)
{
	if (!has_output_dir() || keep < 1)
		return;

	QDir d(QString::fromStdString(output_dir()));
	const QStringList htmls = d.entryList(QStringList() << "lt-*.html", QDir::Files, QDir::Time);
	for (int i = keep; i < htmls.size(); ++i) {
		const QString fn = htmls.at(i);
		d.remove(fn);
	}
}

// -------------------------
// Defaults
// -------------------------
static lower_third_cfg default_cfg()
{
	lower_third_cfg c;
	c.id = new_id();
	c.label = "Lower Third Label";
	c.order = 0;
	c.title = "New Lower Third";
	c.subtitle = "Subtitle";
	c.profile_picture.clear();
	c.anim_in_sound.clear();
	c.anim_out_sound.clear();

	c.title_size = 46;
	c.subtitle_size = 24;

	c.avatar_width = 100;
	c.avatar_height = 100;

	c.anim_in = "animate__fadeInUp";
	c.anim_out = "animate__fadeOutDown";

	c.font_family = "Inter";
	c.lt_position = "lt-pos-bottom-left";

	c.primary_color = "#111827";
	c.secondary_color = "#1F2937";
	c.title_color = "#F9FAFB";
	c.subtitle_color = "#D1D5DB";
	c.opacity = 85;
	c.radius = 5;

	c.html_template =
		R"HTML(
<div class="slt-card" data-slt-root>
  <div class="slt-bg" aria-hidden="true"></div>

  <div class="slt-content">
    <div class="slt-left">
      <img class="slt-avatar" src="{{PROFILE_PICTURE_URL}}" alt="" onerror="this.style.display='none'">
    </div>

    <div class="slt-right">
      <div class="slt-title">{{TITLE}}</div>
      <div class="slt-subtitle">{{SUBTITLE}}</div>
    </div>
  </div>
</div>
)HTML";

	c.css_template =
		R"CSS(
.slt-card {
  position: relative;
  display: inline-block;
  border-radius: {{RADIUS}}px;
  box-shadow: 0 10px 30px rgba(0,0,0,0.35);
  font-family: {{FONT_FAMILY}}, system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
}

/* Background overlay layer (controls opacity without dimming text/avatar) */
.slt-bg {
  position: absolute;
  inset: 0;
  border-radius: inherit;
  background: linear-gradient(135deg, {{PRIMARY_COLOR}}, {{SECONDARY_COLOR}});
  opacity: calc({{OPACITY}} / 100);
  pointer-events: none;
}

.slt-content {
  position: relative;
  z-index: 1;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 18px;
  color: {{TITLE_COLOR}};
}

.slt-avatar {
  width: {{AVATAR_WIDTH}}px;
  height: {{AVATAR_HEIGHT}}px;
  border-radius: 50%;
  object-fit: cover;
  background: rgba(255,255,255,0.08);
  flex-shrink: 0;
}

.slt-avatar:not([src]),
.slt-avatar[src=""],
.slt-avatar[src="./"] {
  display: none !important;
}

.slt-title {
  font-weight: 700;
  font-size: {{TITLE_SIZE}}px;
  line-height: 1.1;
}

.slt-subtitle {
  opacity: 0.9;
  font-size: {{SUBTITLE_SIZE}}px;
  margin-top: 2px;
}
)CSS";

	c.js_template =
		R"JS(
  console.log("LT template running for", root.id || "(no-id)");

  function waitForAnimationEnd(target, name, fallbackMs = 2500) {
    return new Promise((resolve) => {
      let done = false;

      const cleanup = () => {
        if (done) return;
        done = true;
        target.removeEventListener("animationend", onEnd, true);
      };

      const onEnd = (ev) => {
        // strict match (same pattern as your custom scripts)
        if (ev.target !== target) return;
        if (name && ev.animationName !== name) return;
        cleanup();
        resolve();
      };

      target.addEventListener("animationend", onEnd, true);

      setTimeout(() => {
        cleanup();
        resolve();
      }, fallbackMs);
    });
  }

  root.__slt_show = function () {
    // so your custom animation in stuff here
  };

  root.__slt_hide = function () {
	// Do your custom animation out stuff here
    return waitForAnimationEnd(root, "", 3000);
  };
)JS";

	c.repeat_every_sec = 0;
	c.repeat_visible_sec = 0;
	c.hotkey.clear();
	return c;
}

static std::string resolve_in_class(const lower_third_cfg &c)
{
	if (c.anim_in == "custom_handled_in")
		return std::string();
	return c.anim_in;
}

static std::string resolve_out_class(const lower_third_cfg &c)
{
	if (c.anim_out == "custom_handled_out")
		return std::string();
	return c.anim_out;
}

// -------------------------
// HTML/CSS/JS generation
// -------------------------
static std::string build_shared_css()
{
	return R"CSS(
/* Smart Lower Thirds - Base */
:root{ --slt-safe-margin: 40px; --slt-z: 9999; }
html,body{ margin:0; padding:0; background:transparent; overflow:hidden; }
#slt-root{
    position:fixed; inset:0;
    margin:0; padding:0; list-style:none;
    pointer-events:none;
    z-index: var(--slt-z);
}
#slt-root > li{
    position:absolute;
    display: none; /* Start completely hidden */
    pointer-events:none;
}
/* These ensure the display property is toggled correctly */
.slt-visible { display: block !important; }
.slt-hidden  { display: none !important; }
)CSS";
}

static std::string scope_css_best_effort(const lower_third_cfg &c)
{
	std::string css = c.css_template;

	css = replace_all(css, "{{ID}}", c.id);
	css = replace_all(css, "{{PRIMARY_COLOR}}", c.primary_color);
	css = replace_all(css, "{{SECONDARY_COLOR}}", c.secondary_color);
	css = replace_all(css, "{{TITLE_COLOR}}", c.title_color);
	css = replace_all(css, "{{SUBTITLE_COLOR}}", c.subtitle_color);
	// Backward compatibility
	css = replace_all(css, "{{BG_COLOR}}", c.primary_color);
	css = replace_all(css, "{{TEXT_COLOR}}", c.title_color);
	css = replace_all(css, "{{OPACITY}}", std::to_string(c.opacity));
	css = replace_all(css, "{{RADIUS}}", std::to_string(c.radius));
	css = replace_all(css, "{{FONT_FAMILY}}", c.font_family.empty() ? "Inter" : c.font_family);
	css = replace_all(css, "{{TITLE_SIZE}}", std::to_string(c.title_size));
	css = replace_all(css, "{{SUBTITLE_SIZE}}", std::to_string(c.subtitle_size));
	css = replace_all(css, "{{AVATAR_WIDTH}}", std::to_string(c.avatar_width));
	css = replace_all(css, "{{AVATAR_HEIGHT}}", std::to_string(c.avatar_height));

	if (css.find("#" + c.id) != std::string::npos) {
		return "/* ---- " + c.id + " ---- */\n" + css + "\n";
	}

	std::stringstream in(css);
	std::string line;
	std::string out;
	out += "/* ---- " + c.id + " ---- */\n";

	while (std::getline(in, line)) {
		std::string trimmed = line;
		trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(),
							    [](unsigned char ch) { return !std::isspace(ch); }));

		const bool isAt = (!trimmed.empty() && trimmed[0] == '@');
		if (!isAt && line.find('{') != std::string::npos) {
			const auto pos = line.find('{');
			std::string sel = line.substr(0, pos);
			std::string rest = line.substr(pos);

			std::stringstream ss(sel);
			std::string part;
			std::string newSel;
			bool first = true;
			while (std::getline(ss, part, ',')) {
				if (part.find("#" + c.id) == std::string::npos)
					part = " #" + c.id + " " + part;
				if (!first)
					newSel += ",";
				newSel += part;
				first = false;
			}
			out += newSel + rest + "\n";
		} else {
			out += line + "\n";
		}
	}

	return out;
}

static std::string build_base_script(const std::vector<lower_third_cfg> &items)
{
	// Build per-item animation config map consumed by the base JS.
	// Custom Handled contract:
	//   - For IN:  attach root.__slt_show() on the <li>
	//   - For OUT: attach root.__slt_hide() and return a Promise that resolves when exit finishes
	std::string map = "{\n";
	for (const auto &c : items) {
		const bool inCustom  = (c.anim_in == "custom_handled_in");
		const bool outCustom = (c.anim_out == "custom_handled_out");

		const std::string inCls  = inCustom ? std::string() : c.anim_in;
		const std::string outCls = outCustom ? std::string() : c.anim_out;

		// Optional audio cues (relative URLs inside the generated output dir)
		const std::string inSound  = c.anim_in_sound.empty() ? std::string() : ("./" + c.anim_in_sound);
		const std::string outSound = c.anim_out_sound.empty() ? std::string() : ("./" + c.anim_out_sound);

		int delay = 0;

		map += "  \"" + c.id + "\": { "
		       "inCustom: " + std::string(inCustom ? "true" : "false") + ", "
		       "outCustom: " + std::string(outCustom ? "true" : "false") + ", "
		       "inCls: " + (inCls.empty() ? "null" : ("\"" + inCls + "\"")) + ", "
		       "outCls: " + (outCls.empty() ? "null" : ("\"" + outCls + "\"")) + ", "
		       "inSound: " + (inSound.empty() ? "null" : ("\"" + inSound + "\"")) + ", "
		       "outSound: " + (outSound.empty() ? "null" : ("\"" + outSound + "\"")) + ", "
		       "delay: " + std::to_string(delay) + " },\n";
	}
	map += "};\n";

	return std::string(R"JS(
/* Smart Lower Thirds – Base Animation Script (simple polling + per-item transition lock) */
(() => {
  const VISIBLE_URL = "./lt-visible.json";
  const animMap = )JS") +
	       map +
	       std::string(R"JS(
  // Safety bounds (avoid deadlocks if a template forgets to resolve)
  const MAX_CUSTOM_WAIT_MS = 8000;
  const MAX_ANIM_WAIT_MS   = 2000;

  function playCue(url) {
    if (!url || !String(url).trim()) return;
    try {
      const a = new Audio(url);
      a.volume = 1.0;
      // play() may be blocked in some environments; ignore errors
      const p = a.play();
      if (p && typeof p.catch === 'function') p.catch(() => {});
    } catch (e) {}
  }

  function hasAnim(v) { return v && String(v).trim().length > 0; }

  function getHook(el, name) {
    const fn = el && el[name];
    return (typeof fn === "function") ? fn : null;
  }

  function stripAnimate(el) {
    // Remove only what we might have applied (do not destroy author classes).
    try { el.classList.remove("animate__animated"); } catch (e) {}
    const added = Array.isArray(el.__slt_added) ? el.__slt_added : [];
    for (const c of added) {
      try { el.classList.remove(c); } catch (e) {}
    }
    el.__slt_added = [];
    el.style.animationDelay = "";
    el.style.animationDuration = "";
    el.style.animationTimingFunction = "";
  }

  function addAnimClasses(el, cls) {
    el.__slt_added = Array.isArray(el.__slt_added) ? el.__slt_added : [];
    try {
      el.classList.add("animate__animated");
      el.__slt_added.push("animate__animated");
    } catch (e) {}

    String(cls).split(/\s+/).filter(Boolean).forEach(c => {
      try { el.classList.add(c); } catch (e) {}
      el.__slt_added.push(c);
    });
  }

  function waitOwnAnimationEnd(el, timeoutMs) {
    return new Promise((resolve) => {
      let done = false;

      const cleanup = () => {
        if (done) return;
        done = true;
        el.removeEventListener("animationend", onEnd, true);
      };

      const onEnd = (ev) => {
        // Only end when the <li> itself ends its animation (ignore child animations)
        if (ev.target !== el) return;
        cleanup();
        resolve();
      };

      el.addEventListener("animationend", onEnd, true);
      setTimeout(() => { cleanup(); resolve(); }, timeoutMs);
    });
  }

  async function runHookWithTimeout(el, name, timeoutMs) {
    const fn = getHook(el, name);
    if (!fn) return;

    try {
      const ret = fn.call(el);
      if (ret && typeof ret.then === "function") {
        await Promise.race([
          ret,
          new Promise(res => setTimeout(res, timeoutMs))
        ]);
      }
    } catch (e) {
      // Swallow template errors: base script must remain operational.
    }
  }

  function setMounted(el, mounted) {
    if (mounted) {
      el.style.display = "block";
      el.classList.add("slt-visible");
      el.classList.remove("slt-hidden");
    } else {
      el.classList.remove("slt-visible");
      el.classList.add("slt-hidden");
      el.style.display = "none";
    }
  }

  async function doShow(el, cfg) {
    setMounted(el, true);
    stripAnimate(el);

    // Audio cue (handled here so it works even if the LT template does not implement playback)
    if (cfg && cfg.inSound) playCue(cfg.inSound);

    if (cfg && cfg.inCustom) {
      await runHookWithTimeout(el, "__slt_show", MAX_CUSTOM_WAIT_MS);
      return;
    }

    if (cfg && hasAnim(cfg.inCls)) {
      if (cfg.delay > 0) el.style.animationDelay = cfg.delay + "ms";
      addAnimClasses(el, cfg.inCls);
      await waitOwnAnimationEnd(el, MAX_ANIM_WAIT_MS);
      stripAnimate(el);
      el.style.animationDelay = "";
    }
  }

  async function doHide(el, cfg) {
    stripAnimate(el);

    // Audio cue (handled here so it works even if the LT template does not implement playback)
    if (cfg && cfg.outSound) playCue(cfg.outSound);

    if (cfg && cfg.outCustom) {
      // Wait for the template-driven exit animation before unmounting the <li>
      await runHookWithTimeout(el, "__slt_hide", MAX_CUSTOM_WAIT_MS);
      setMounted(el, false);
      return;
    }

    if (cfg && hasAnim(cfg.outCls)) {
      addAnimClasses(el, cfg.outCls);
      await waitOwnAnimationEnd(el, MAX_ANIM_WAIT_MS);
      stripAnimate(el);
    }

    setMounted(el, false);
  }

  function enqueue(el, job) {
    // Serialize transitions per <li> so polling never overlaps operations.
    el.__slt_queue = (el.__slt_queue || Promise.resolve())
      .then(job)
      .catch(() => {}); // never break the chain
  }

  async function tick() {
    let visibleIds;
    try {
      const r = await fetch(VISIBLE_URL + "?t=" + Date.now(), { cache: "no-store" });
      visibleIds = await r.json();
      if (!Array.isArray(visibleIds)) return;
    } catch (e) {
      return;
    }

    const visibleSet = new Set(visibleIds.map(String));
    const els = Array.from(document.querySelectorAll("#slt-root > li[id]"));

    for (const el of els) {
      const cfg = animMap[el.id] || {};
      const want = visibleSet.has(el.id);

      // Store desired state
      el.dataset.want = want ? "1" : "0";

      const isMounted = el.classList.contains("slt-visible") || el.style.display === "block";

      // Already in desired mounted state
      if (want && isMounted) continue;
      if (!want && !isMounted) continue;

      // Avoid enqueuing duplicates while one is active
      if (el.dataset.busy === "1") continue;

      el.dataset.busy = "1";
      enqueue(el, async () => {
        try {
          // Re-check desire at execution time (poll may have changed)
          const stillWant = el.dataset.want === "1";
          if (stillWant) await doShow(el, cfg);
          else await doHide(el, cfg);
        } finally {
          el.dataset.busy = "0";
        }
      });
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    tick();
    setInterval(tick, 350);
  });
})();
)JS");
}

static std::string build_item_script(const lower_third_cfg &c)
{
	std::string js = c.js_template;
	js = replace_all(js, "{{ID}}", c.id);
	js = replace_all(js, "{{ANIM_IN}}", c.anim_in);
	js = replace_all(js, "{{ANIM_OUT}}", c.anim_out);
	js = replace_all(js, "{{TITLE}}", c.title);
	js = replace_all(js, "{{SUBTITLE}}", c.subtitle);
	const std::string sIn = c.anim_in_sound.empty() ? "" : ("./" + c.anim_in_sound);
	const std::string sOut = c.anim_out_sound.empty() ? "" : ("./" + c.anim_out_sound);
	js = replace_all(js, "{{SOUND_IN_URL}}", sIn);
	js = replace_all(js, "{{SOUND_OUT_URL}}", sOut);

	std::string out;
	out += "\n/* ---- " + c.id + " ---- */\n";
	out += "(() => {\n";
	out += "  const root = document.getElementById(\"" + c.id + "\");\n";
	out += "  if (!root) return;\n";
	out += "  try {\n";
	out += js;
	out += "\n  } catch(e) { console.error(\"SLT script error for " + c.id + "\", e); }\n";
	out += "})();\n";
	return out;
}

static std::string build_full_html(const std::string &ts, const std::string &cssFile, const std::string &jsFile)
{
	std::string html;
	html += "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\"/>\n";
	html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n";
	html += "<link rel=\"stylesheet\" href=\"./" + cssFile + "?v=" + ts + "\"/>\n";

	const std::string animateLocalAbs = path_animate_css();
	if (!animateLocalAbs.empty() && file_exists(animateLocalAbs)) {
		LOGI("Using local animate.min.css");
		html += "<link rel=\"stylesheet\" href=\"./animate.min.css\"/>\n";
	} else {
		LOGI("Using CDN animate.css (local animate.min.css not found)");
		html += "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/animate.css/4.1.1/animate.min.css\"/>\n";
	}

	html += "</head>\n<body>\n<ul id=\"slt-root\">\n";

	for (const auto &c : g_items) {
		std::string inner = c.html_template;
		inner = replace_all(inner, "{{ID}}", c.id);
		inner = replace_all(inner, "{{TITLE}}", c.title);
		inner = replace_all(inner, "{{SUBTITLE}}", c.subtitle);
		inner = replace_all(inner, "{{PRIMARY_COLOR}}", c.primary_color);
		inner = replace_all(inner, "{{SECONDARY_COLOR}}", c.secondary_color);
		inner = replace_all(inner, "{{TITLE_COLOR}}", c.title_color);
		inner = replace_all(inner, "{{SUBTITLE_COLOR}}", c.subtitle_color);

		inner = replace_all(inner, "{{BG_COLOR}}", c.primary_color);
		inner = replace_all(inner, "{{TEXT_COLOR}}", c.title_color);
		inner = replace_all(inner, "{{OPACITY}}", std::to_string(c.opacity));
		inner = replace_all(inner, "{{RADIUS}}", std::to_string(c.radius));
		inner = replace_all(inner, "{{FONT_FAMILY}}", c.font_family.empty() ? "Inter" : c.font_family);

		const std::string pic = c.profile_picture.empty() ? "./" : ("./" + c.profile_picture);
		inner = replace_all(inner, "{{PROFILE_PICTURE_URL}}", pic);

		const std::string sIn = c.anim_in_sound.empty() ? "" : ("./" + c.anim_in_sound);
		const std::string sOut = c.anim_out_sound.empty() ? "" : ("./" + c.anim_out_sound);
		inner = replace_all(inner, "{{SOUND_IN_URL}}", sIn);
		inner = replace_all(inner, "{{SOUND_OUT_URL}}", sOut);


		if (inner.find("onerror") == std::string::npos) {
			inner = replace_all(inner, "<img ", "<img onerror=\"this.style.display='none'\" ");
		}

		const bool customMode = (c.anim_in == "custom_handled_in") || (c.anim_out == "custom_handled_out");

		html += "  <li id=\"" + c.id + "\" class=\"" + c.lt_position + "\"" +
		        (customMode ? " data-slt-mode=\"custom\"" : "") + ">";
		html += inner;
		html += "</li>\n";
	}

	html += "</ul>\n<script defer src=\"./" + jsFile + "?v=" + ts + "\"></script>\n</body>\n</html>\n";
	return html;
}

// -------------------------
// Public
// -------------------------
bool has_output_dir()
{
	return !g_output_dir.empty();
}

std::string output_dir()
{
	return g_output_dir;
}

std::string path_state_json()
{
	return has_output_dir() ? join_path(g_output_dir, "lt-state.json") : "";
}

std::string path_visible_json()
{
	return has_output_dir() ? join_path(g_output_dir, "lt-visible.json") : "";
}

std::string path_styles_css()
{
	return has_output_dir() ? join_path(g_output_dir, "lt.css") : "";
}

std::string path_scripts_js()
{
	return has_output_dir() ? join_path(g_output_dir, "lt.js") : "";
}

std::string path_animate_css()
{
	return has_output_dir() ? join_path(g_output_dir, "animate.min.css") : "";
}

std::string now_timestamp_string()
{
	const qint64 ts = QDateTime::currentMSecsSinceEpoch();
	return std::to_string((long long)ts);
}

std::string new_id()
{
	static std::mt19937_64 rng{std::random_device{}()};
	static std::uniform_int_distribution<uint64_t> dist;
	uint64_t a = dist(rng);
	uint64_t b = dist(rng);

	std::ostringstream ss;
	ss << "lt_" << std::hex << a << b;
	return sanitize_id(ss.str());
}

std::vector<lower_third_cfg> &all()
{
	return g_items;
}

const std::vector<lower_third_cfg> &all_const()
{
	return g_items;
}

lower_third_cfg *get_by_id(const std::string &id)
{
	for (auto &c : g_items)
		if (c.id == id)
			return &c;
	return nullptr;
}


// -------------------------
// Carousel state access
// -------------------------
std::vector<carousel_cfg> &carousels()
{
	return g_carousels;
}

const std::vector<carousel_cfg> &carousels_const()
{
	return g_carousels;
}

carousel_cfg *get_carousel_by_id(const std::string &id)
{
	const std::string sid = sanitize_id(id);
	for (auto &c : g_carousels) {
		if (c.id == sid)
			return &c;
	}
	return nullptr;
}

std::vector<std::string> carousels_containing(const std::string &lower_third_id)
{
	std::vector<std::string> out;
	const std::string sid = sanitize_id(lower_third_id);
	for (const auto &c : g_carousels) {
		for (const auto &mid : c.members) {
			if (mid == sid) {
				out.push_back(c.id);
				break;
			}
		}
	}
	return out;
}


std::vector<std::string> visible_ids()
{
	return g_visible;
}

bool is_visible(const std::string &id)
{
	return std::find(g_visible.begin(), g_visible.end(), id) != g_visible.end();
}

// -------------------------
// Visible set (NOSAVE / NOEVENT)
// -------------------------
void set_visible_nosave(const std::string &id, bool visible)
{
	if (id.empty())
		return;

	if (visible) {
		if (!is_visible(id))
			g_visible.push_back(id);
	} else {
		g_visible.erase(std::remove(g_visible.begin(), g_visible.end(), id), g_visible.end());
	}
}

void toggle_visible_nosave(const std::string &id)
{
	set_visible_nosave(id, !is_visible(id));
}

// -------------------------
// Visible set (PERSIST + NOTIFY)
// -------------------------
bool set_visible_persist(const std::string &id, bool visible)
{
	if (!has_output_dir() || id.empty())
		return false;

	if (!get_by_id(id))
		return false;

	const bool before = is_visible(id);
	if (before == visible) {
		return true;
	}

	set_visible_nosave(id, visible);
	if (!save_visible_json())
		return false;

	core_event ev;
	ev.type = event_type::VisibilityChanged;
	ev.id = id;
	ev.visible = visible;
	ev.visible_ids = visible_ids();
	emit_event(ev);

	return true;
}

bool toggle_visible_persist(const std::string &id)
{
	if (!has_output_dir() || id.empty())
		return false;

	if (!get_by_id(id))
		return false;

	const bool after = !is_visible(id);
	return set_visible_persist(id, after);
}

// -------------------------
// Artifacts files
// -------------------------
bool ensure_output_artifacts_exist()
{
	if (!has_output_dir())
		return false;

	ensure_dir(output_dir());

	if (!QFile::exists(QString::fromStdString(path_state_json()))) {
		g_items.clear();
		save_state_json();
	}
	if (!QFile::exists(QString::fromStdString(path_visible_json()))) {
		g_visible.clear();
		save_visible_json();
	}
	if (!QFile::exists(QString::fromStdString(path_styles_css()))) {
		write_text_file(path_styles_css(), "/* generated */\n");
	}
	if (!QFile::exists(QString::fromStdString(path_scripts_js()))) {
		write_text_file(path_scripts_js(), "/* generated */\n");
	}

	return true;
}

bool load_state_json()
{
	if (!has_output_dir())
		return false;

	const std::string p = path_state_json();
	if (!QFile::exists(QString::fromStdString(p))) {
		g_items.clear();
		g_carousels.clear();
		return true;
	}

	const std::string txt = read_text_file(p);
	if (txt.empty()) {
		g_items.clear();
		g_carousels.clear();
		return true;
	}

	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(txt), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		LOGW("Invalid lt-state.json; reset");
		g_items.clear();
		g_carousels.clear();
		return false;
	}

	const QJsonObject root = doc.object();
	const QJsonArray items = root.value("items").toArray();
	const QJsonArray cars = root.value("carousels").toArray();

	std::vector<lower_third_cfg> out;
	out.reserve((size_t)items.size());

	for (const QJsonValue v : items) {
		if (!v.isObject())
			continue;
		const QJsonObject o = v.toObject();

		lower_third_cfg c;
		c.id = sanitize_id(o.value("id").toString().toStdString());
		if (c.id.empty())
			c.id = new_id();

		c.label = o.value("label").toString().toStdString();
		c.order = o.value("order").toInt(-1);

		c.title = o.value("title").toString().toStdString();
		c.subtitle = o.value("subtitle").toString().toStdString();
		c.profile_picture = o.value("profile_picture").toString().toStdString();
		c.anim_in_sound = o.value("anim_in_sound").toString().toStdString();
		c.anim_out_sound = o.value("anim_out_sound").toString().toStdString();

		c.title_size = o.value("title_size").toInt(46);
		c.subtitle_size = o.value("subtitle_size").toInt(24);
		c.title_size = std::max(6, std::min(200, c.title_size));
		c.subtitle_size = std::max(6, std::min(200, c.subtitle_size));

		c.avatar_width = o.value("avatar_width").toInt(100);
		c.avatar_height = o.value("avatar_height").toInt(100);
		c.avatar_width = std::max(10, std::min(400, c.avatar_width));
		c.avatar_height = std::max(10, std::min(400, c.avatar_height));

		c.anim_in = o.value("anim_in").toString().toStdString();
		c.anim_out = o.value("anim_out").toString().toStdString();

		c.font_family = o.value("font_family").toString().toStdString();
		c.lt_position = o.value("lt_position").toString().toStdString();

		c.primary_color = o.value("primary_color").toString().toStdString();
		c.secondary_color = o.value("secondary_color").toString().toStdString();
		c.title_color = o.value("title_color").toString().toStdString();
		c.subtitle_color = o.value("subtitle_color").toString().toStdString();

		// Backward compatibility
		if (c.primary_color.empty())
			c.primary_color = o.value("bg_color").toString().toStdString();
		if (c.title_color.empty())
			c.title_color = o.value("text_color").toString().toStdString();
		if (c.secondary_color.empty())
			c.secondary_color = c.primary_color;
		if (c.subtitle_color.empty())
			c.subtitle_color = c.title_color;
		c.opacity = o.value("opacity").toInt(0);
		c.radius = o.value("radius").toInt(0);

		if(c.opacity < 0 || c.opacity > 100)
			c.opacity = 85;
		if(c.radius < 0 || c.radius > 100)
			c.radius = 5;

		c.html_template = o.value("html_template").toString().toStdString();
		c.css_template = o.value("css_template").toString().toStdString();
		c.js_template = o.value("js_template").toString().toStdString();

		c.hotkey = o.value("hotkey").toString().toStdString();
		c.repeat_every_sec = o.value("repeat_every_sec").toInt(0);
		c.repeat_visible_sec = o.value("repeat_visible_sec").toInt(0);

		if (c.repeat_every_sec < 0)
			c.repeat_every_sec = 0;
		if (c.repeat_visible_sec < 0)
			c.repeat_visible_sec = 0;

		if (c.html_template.empty() || c.css_template.empty()) {
			auto d = default_cfg();
			if (c.html_template.empty())
				c.html_template = d.html_template;
			if (c.css_template.empty())
				c.css_template = d.css_template;
			if (c.js_template.empty())
				c.js_template = d.js_template;
		}

		if (c.lt_position.empty())
			c.lt_position = "lt-pos-bottom-left";
		if (c.anim_in.empty())
			c.anim_in = "animate__fadeInUp";
		if (c.anim_out.empty())
			c.anim_out = "animate__fadeOutDown";
		if (c.primary_color.empty())
			c.primary_color = "#111827";
		if (c.secondary_color.empty())
			c.secondary_color = "#1F2937";
		if (c.title_color.empty())
			c.title_color = "#F9FAFB";
		if (c.subtitle_color.empty())
			c.subtitle_color = "#D1D5DB";

		if (c.label.empty())
			c.label = c.title.empty() ? c.id : c.title;

		out.push_back(std::move(c));
	}

	int nextOrder = 0;
	for (auto &c : out) {
		if (c.order < 0)
			c.order = nextOrder;
		nextOrder = std::max(nextOrder, c.order + 1);
	}
	std::sort(out.begin(), out.end(), [](const lower_third_cfg &a, const lower_third_cfg &b) {
		if (a.order != b.order)
			return a.order < b.order;
		return a.id < b.id;
	});

	
// Carousels (dock-only)
std::vector<carousel_cfg> outCars;
outCars.reserve((size_t)cars.size());
for (const QJsonValue v : cars) {
	if (!v.isObject())
		continue;
	const QJsonObject o = v.toObject();

	carousel_cfg c;
	c.id = sanitize_id(o.value("id").toString().toStdString());
	if (c.id.empty())
		c.id = new_id();

	c.title = o.value("title").toString().toStdString();
	c.order = o.value("order").toInt(-1);
	c.order_mode = o.value("order_mode").toInt(0);
	c.loop = o.value("loop").toBool(true);
	// Defaults are dock-only and can be changed via the Manage Carousels dialog.
	// Interval: 5000ms, Visible: 15000ms
	c.visible_ms = o.value("visible_ms").toInt(15000);
	c.interval_ms = o.value("interval_ms").toInt(5000);
	c.dock_color = o.value("dock_color").toString().toStdString();

	if (c.visible_ms < 250)
		c.visible_ms = 250;
	if (c.interval_ms < 0)
		c.interval_ms = 0;
	if (c.order_mode != 1)
		c.order_mode = 0;
	if (c.order_mode < 0 || c.order_mode > 1)
		c.order_mode = 0;

	const QJsonArray mem = o.value("members").toArray();
	for (const QJsonValue mv : mem) {
		const std::string mid = sanitize_id(mv.toString().toStdString());
		if (!mid.empty())
			c.members.push_back(mid);
	}

	if (c.title.empty())
		c.title = "Carousel";

	outCars.push_back(std::move(c));
}

int nextCarOrder = 0;
for (auto &c : outCars) {
	if (c.order < 0)
		c.order = nextCarOrder;
	nextCarOrder = std::max(nextCarOrder, c.order + 1);
}
std::sort(outCars.begin(), outCars.end(), [](const carousel_cfg &a, const carousel_cfg &b) {
	if (a.order != b.order)
		return a.order < b.order;
	return a.id < b.id;
});

	g_carousels = std::move(outCars);

	// Enforce invariant: a lower third can belong to at most one carousel.
	// If state contains duplicates, keep the first carousel (by current sort order) and drop from later ones.
	{
		std::unordered_set<std::string> claimed;
		for (auto &car : g_carousels) {
			std::vector<std::string> uniq;
			uniq.reserve(car.members.size());
			for (const auto &midRaw : car.members) {
				const std::string mid = sanitize_id(midRaw);
				if (mid.empty())
					continue;
				if (claimed.find(mid) != claimed.end())
					continue;
				claimed.insert(mid);
				uniq.push_back(mid);
			}
			car.members = std::move(uniq);
		}
	}

	g_items = std::move(out);

	// Ensure per-item repeat timers are disabled for any lower third that belongs to a carousel.
	// This keeps legacy state files consistent with the carousel runner logic.
	for (const auto &car : g_carousels) {
		for (const auto &mid : car.members) {
			if (auto *lt = get_by_id(mid)) {
				lt->repeat_every_sec = 0;
				lt->repeat_visible_sec = 0;
			}
		}
	}
	return true;
}

bool save_state_json()
{
	if (!has_output_dir())
		return false;

	QJsonObject root;
	root["version"] = 3;

	QJsonArray items;
	for (const auto &c : g_items) {
		QJsonObject o;
		o["id"] = QString::fromStdString(c.id);
		o["label"] = QString::fromStdString(c.label);
		o["order"] = c.order;
		o["title"] = QString::fromStdString(c.title);
		o["subtitle"] = QString::fromStdString(c.subtitle);
		o["profile_picture"] = QString::fromStdString(c.profile_picture);
		o["anim_in_sound"] = QString::fromStdString(c.anim_in_sound);
		o["anim_out_sound"] = QString::fromStdString(c.anim_out_sound);

		o["title_size"] = c.title_size;
		o["subtitle_size"] = c.subtitle_size;
		o["avatar_width"] = c.avatar_width;
		o["avatar_height"] = c.avatar_height;

		o["anim_in"] = QString::fromStdString(c.anim_in);
		o["anim_out"] = QString::fromStdString(c.anim_out);

		o["font_family"] = QString::fromStdString(c.font_family);
		o["lt_position"] = QString::fromStdString(c.lt_position);

		o["primary_color"] = QString::fromStdString(c.primary_color);
		o["secondary_color"] = QString::fromStdString(c.secondary_color);
		o["title_color"] = QString::fromStdString(c.title_color);
		o["subtitle_color"] = QString::fromStdString(c.subtitle_color);

		// Backward compatibility keys (older versions)
		o["bg_color"] = QString::fromStdString(c.primary_color);
		o["text_color"] = QString::fromStdString(c.title_color);
		o["opacity"] = c.opacity;
		o["radius"] = c.radius;

		o["html_template"] = QString::fromStdString(c.html_template);
		o["css_template"] = QString::fromStdString(c.css_template);
		o["js_template"] = QString::fromStdString(c.js_template);

		o["hotkey"] = QString::fromStdString(c.hotkey);
		o["repeat_every_sec"] = c.repeat_every_sec;
		o["repeat_visible_sec"] = c.repeat_visible_sec;

		items.append(o);
	}

	
QJsonArray cars;
for (const auto &c : g_carousels) {
	QJsonObject o;
	o["id"] = QString::fromStdString(c.id);
	o["title"] = QString::fromStdString(c.title);
	o["order"] = c.order;
	o["order_mode"] = c.order_mode;
	o["loop"] = c.loop;
	o["visible_ms"] = c.visible_ms;
	o["interval_ms"] = c.interval_ms;
	o["dock_color"] = QString::fromStdString(c.dock_color);

	QJsonArray mem;
	for (const auto &mid : c.members)
		mem.append(QString::fromStdString(mid));
	o["members"] = mem;

	cars.append(o);
}
root["carousels"] = cars;

	root["items"] = items;

	const QJsonDocument doc(root);
	return write_text_file(path_state_json(), doc.toJson(QJsonDocument::Indented).toStdString());
}

bool load_visible_json()
{
	if (!has_output_dir())
		return false;

	const std::string p = path_visible_json();
	if (!QFile::exists(QString::fromStdString(p))) {
		g_visible.clear();
		return true;
	}

	const std::string txt = read_text_file(p);
	if (txt.empty()) {
		g_visible.clear();
		return true;
	}

	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(txt), &err);
	if (err.error != QJsonParseError::NoError) {
		LOGW("Invalid lt-visible.json; reset");
		g_visible.clear();
		return false;
	}

	std::vector<std::string> ids;
	if (doc.isArray()) {
		const QJsonArray a = doc.array();
		for (const QJsonValue v : a) {
			if (v.isString())
				ids.push_back(sanitize_id(v.toString().toStdString()));
		}
	}

	std::vector<std::string> keep;
	keep.reserve(ids.size());
	for (const auto &id : ids)
		if (get_by_id(id))
			keep.push_back(id);

	g_visible = std::move(keep);
	return true;
}

bool save_visible_json()
{
	if (!has_output_dir())
		return false;

	std::vector<std::string> ids = g_visible;
	ids.erase(std::remove_if(ids.begin(), ids.end(), [](const std::string &s) { return s.empty(); }), ids.end());
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

	QJsonArray a;
	for (const auto &id : ids)
		a.append(QString::fromStdString(id));

	const QJsonDocument doc(a);
	return write_text_file(path_visible_json(), doc.toJson(QJsonDocument::Indented).toStdString());
}

static bool regenerate_merged_css_js(const std::string &ts, std::string &outCssFile, std::string &outJsFile)
{
	if (!has_output_dir())
		return false;

	outCssFile = bundle_styles_name(ts);
	outJsFile  = bundle_scripts_name(ts);

	std::string css;
	css += build_shared_css();

	css += R"CSS(

/* Position classes */
.lt-pos-bottom-left  {
  left: var(--slt-safe-margin);
  bottom: var(--slt-safe-margin);
}

.lt-pos-bottom-right {
  right: var(--slt-safe-margin);
  bottom: var(--slt-safe-margin);
}

.lt-pos-top-left {
  left: var(--slt-safe-margin);
  top: var(--slt-safe-margin);
}

.lt-pos-top-right {
  right: var(--slt-safe-margin);
  top: var(--slt-safe-margin);
}

.lt-pos-center {
  left: 50%;
  top: 50%;
  transform: translate(-50%, -50%);
}

.lt-pos-top-center {
  left: 50%;
  top: var(--slt-safe-margin);
  transform: translateX(-50%);
}

.lt-pos-bottom-center {
  left: 50%;
  bottom: var(--slt-safe-margin);
  transform: translateX(-50%);
}

)CSS";

	css += "\n/* Per-LT scoped styles */\n";

	// Keyframes registry: dedupe by name+content, resolve conflicts by renaming per-LT.
	// - If same name + identical content => keep one.
	// - If same name + different content => rename to <name>_<ltid> and rewrite references in that LT CSS.
	std::unordered_map<std::string, std::string> kfNameToNorm;
	std::unordered_map<std::string, std::string> kfNameToBlock;
	std::vector<std::string> kfOrder;

	for (const auto &c : g_items) {
		// Expand placeholders up-front (so extracted keyframes are final).
		std::string per = c.css_template;
		per = replace_all(per, "{{ID}}", c.id);
		per = replace_all(per, "{{PRIMARY_COLOR}}", c.primary_color);
		per = replace_all(per, "{{SECONDARY_COLOR}}", c.secondary_color);
		per = replace_all(per, "{{TITLE_COLOR}}", c.title_color);
		per = replace_all(per, "{{SUBTITLE_COLOR}}", c.subtitle_color);
		// Backward compatibility
		per = replace_all(per, "{{BG_COLOR}}", c.primary_color);
		per = replace_all(per, "{{TEXT_COLOR}}", c.title_color);
		per = replace_all(per, "{{OPACITY}}", std::to_string(c.opacity));
		per = replace_all(per, "{{RADIUS}}", std::to_string(c.radius));
		per = replace_all(per, "{{FONT_FAMILY}}", c.font_family.empty() ? "Inter" : c.font_family);
		per = replace_all(per, "{{TITLE_SIZE}}", std::to_string(c.title_size));
		per = replace_all(per, "{{SUBTITLE_SIZE}}", std::to_string(c.subtitle_size));

		// Extract keyframes so scope_css_best_effort never touches 0%/from/to selectors.
		std::vector<extracted_keyframes> extracted;
		extract_keyframes_blocks(per, extracted);

		// Register/dedupe keyframes and resolve name collisions.
		for (auto &kf : extracted) {
			// If no parsed name, treat as "content-only" and append if unique.
			if (kf.name.empty()) {
				const std::string sig = kf.norm;
				bool exists = false;
				for (const auto &kv : kfNameToNorm) {
					if (kv.second == sig) {
						exists = true;
						break;
					}
				}
				if (!exists) {
					const std::string anonName = "kf_" + c.id + "_" + std::to_string(kfOrder.size());
					kfNameToNorm[anonName] = sig;
					kfNameToBlock[anonName] = kf.block;
					kfOrder.push_back(anonName);
				}
				continue;
			}

			auto it = kfNameToNorm.find(kf.name);
			if (it == kfNameToNorm.end()) {
				// First appearance of this name
				kfNameToNorm[kf.name] = kf.norm;
				kfNameToBlock[kf.name] = kf.block;
				kfOrder.push_back(kf.name);
				continue;
			}

			if (it->second == kf.norm) {
				// Exact duplicate: ignore
				continue;
			}

			// Conflict: same name, different content => rename this LT's keyframes
			const std::string oldName = kf.name;
			const std::string newName = oldName + "_" + c.id;

			// Update header in the extracted block (best-effort, deterministic)
			const std::string fromHdr = kf.at_rule + std::string(" ") + oldName;
			const std::string toHdr = kf.at_rule + std::string(" ") + newName;
			kf.block = replace_all(kf.block, fromHdr, toHdr);
			kf.name = newName;
			kf.norm = normalize_ws_no_space(kf.block);

			// Rewrite references in this LT CSS (animation-name and shorthand cases)
			per = replace_whole_ident(per, oldName, newName);

			// Register renamed variant
			kfNameToNorm[kf.name] = kf.norm;
			kfNameToBlock[kf.name] = kf.block;
			kfOrder.push_back(kf.name);
		}

		// Now scope per-LT selectors (keyframes removed; safe)
		lower_third_cfg tmp = c;
		tmp.css_template = per;
		css += "\n" + scope_css_best_effort(tmp);
	}

	// Append deduped keyframes at the end of lt-styles.css
	css += "\n/* Keyframes (deduped) */\n";
	{
		std::unordered_set<std::string> emitted;
		for (const auto &name : kfOrder) {
			if (!emitted.insert(name).second)
				continue;
			auto it = kfNameToBlock.find(name);
			if (it != kfNameToBlock.end()) {
				css += "\n" + it->second + "\n";
			}
		}
	}

	const std::string cssPath = bundle_styles_path(ts);
	if (cssPath.empty() || !write_text_file(cssPath, css)) {
		LOGW("Failed writing %s", cssPath.empty() ? "<empty css path>" : cssPath.c_str());
		return false;
	}

	std::string js;
	js += build_base_script(g_items);
	js += "\n\n/* Per-LT scripts */\n";
	for (const auto &c : g_items)
		js += build_item_script(c);

	const std::string jsPath = bundle_scripts_path(ts);
	if (jsPath.empty() || !write_text_file(jsPath, js)) {
		LOGW("Failed writing %s", jsPath.empty() ? "<empty js path>" : jsPath.c_str());
		return false;
	}

	return true;
}

static std::string generate_bundle_html(const std::string &ts, const std::string &cssFile, const std::string &jsFile)
{
	if (!has_output_dir())
		return {};

	const std::string abs = bundle_html_path(ts);
	if (abs.empty())
		return {};

	if (!write_text_file(abs, build_full_html(ts, cssFile, jsFile)))
		return {};
	return abs;
}

// -------------------------
// Browser source helpers (combo-box workflow)
// -------------------------

static obs_source_t *get_target_browser_source()
{
	if (g_target_browser_source.empty())
		return nullptr;

	return obs_get_source_by_name(g_target_browser_source.c_str());
}

std::vector<std::string> list_browser_source_names()
{
	std::vector<std::string> out;

	auto enum_cb = [](void *param, obs_source_t *src) -> bool {
		if (!src)
			return true;

		const char *id = obs_source_get_id(src);
		if (!id)
			return true;

		if (std::string(id) != sltBrowserSourceId)
			return true;

		const char *name = obs_source_get_name(src);
		if (name && *name) {
			auto *vec = static_cast<std::vector<std::string> *>(param);
			vec->push_back(std::string(name));
		}
		return true;
	};

	obs_enum_sources(enum_cb, &out);

	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

std::string target_browser_source_name()
{
	return g_target_browser_source;
}

bool set_target_browser_source_name(const std::string &name)
{
	g_target_browser_source = name;
	return save_global_config();
}

int target_browser_width()
{
	return g_target_browser_width;
}

int target_browser_height()
{
	return g_target_browser_height;
}

bool set_target_browser_dimensions(int width, int height)
{
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	g_target_browser_width = width;
	g_target_browser_height = height;

	obs_source_t *src = get_target_browser_source();
	if (src) {
		obs_data_t *s = obs_source_get_settings(src);
		obs_data_set_int(s, "width", (int64_t)g_target_browser_width);
		obs_data_set_int(s, "height", (int64_t)g_target_browser_height);
		obs_source_update(src, s);
		obs_data_release(s);
		obs_source_release(src);
	}

	return save_global_config();
}

bool dock_exclusive_mode()
{
	return g_dock_exclusive_mode;
}

bool set_dock_exclusive_mode(bool enabled)
{
	g_dock_exclusive_mode = enabled;
	return save_global_config();
}

bool target_browser_source_exists()
{
	obs_source_t *src = get_target_browser_source();
	if (!src)
		return false;

	const char *id = obs_source_get_id(src);
	const bool ok = (id && std::string(id) == sltBrowserSourceId);

	obs_source_release(src);
	return ok;
}

static void refreshSourceSettings(obs_source_t *s)
{
	if (!s)
		return;

	obs_data_t *data = obs_source_get_settings(s);
	obs_source_update(s, data);
	obs_data_release(data);

	if (strcmp(obs_source_get_id(s), "browser_source") == 0) {
		obs_properties_t *sourceProperties = obs_source_properties(s);
		obs_property_t *property = obs_properties_get(sourceProperties, "refreshnocache");
		if (property)
			obs_property_button_clicked(property, s);
		obs_properties_destroy(sourceProperties);
	}
}

bool swap_target_browser_source_to_file(const std::string &absoluteHtmlPath)
{
	if (absoluteHtmlPath.empty())
		return false;

	obs_source_t *src = get_target_browser_source();
	if (!src) {
		if (g_target_browser_source.empty())
			LOGW("No target Browser Source selected.");
		else
			LOGW("Target Browser Source '%s' not found.", g_target_browser_source.c_str());
		return false;
	}

	const char *id = obs_source_get_id(src);
	if (!id || std::string(id) != sltBrowserSourceId) {
		LOGW("Target source '%s' is not a Browser Source.", g_target_browser_source.c_str());
		obs_source_release(src);
		return false;
	}

	obs_data_t *s = obs_source_get_settings(src);
	const char *prevPathC = obs_data_get_string(s, "local_file");
	const std::string prevPath = prevPathC ? std::string(prevPathC) : std::string();

	// If OBS thinks the path did not change, some builds won't fully reload the
	// Browser Source even after triggering the refresh button. Force a path flip.
	if (!prevPath.empty() && prevPath == absoluteHtmlPath) {
		obs_data_set_string(s, "local_file", "");
		obs_source_update(src, s);
	}
	obs_data_set_bool(s, "is_local_file", true);
	obs_data_set_string(s, "local_file", absoluteHtmlPath.c_str());

	// Ensure Browser Source audio is controllable via OBS ("Control audio via OBS").
	// OBS has used different internal keys across versions; setting the common ones is safe.
	obs_data_set_bool(s, "is_control_audio", true);
	obs_data_set_bool(s, "control_audio", true);
	obs_data_set_bool(s, "reroute_audio", true);

	obs_data_set_bool(s, "smart_lt_managed", true);
	obs_data_set_int(s, "width", (int64_t)g_target_browser_width);
	obs_data_set_int(s, "height", (int64_t)g_target_browser_height);
	obs_source_update(src, s);

	obs_data_release(s);

	refreshSourceSettings(src);
	obs_source_release(src);
	return true;
}

// -------------------------
// High-level triggers
// -------------------------
bool rebuild_and_swap()
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();

	// Generate versioned artifacts on each apply. This avoids a common Windows/CEF
	// failure mode where the currently loaded local files are kept open and can't
	// be truncated/overwritten until OBS is restarted.
	const std::string ts = now_timestamp_string();
	std::string cssFile, jsFile;
	if (!regenerate_merged_css_js(ts, cssFile, jsFile))
		return false;

	const std::string newHtml = generate_bundle_html(ts, cssFile, jsFile);
	if (newHtml.empty())
		return false;

	cleanup_old_bundles(1);

	if (target_browser_source_exists()) {
		swap_target_browser_source_to_file(newHtml);
	} else {
		if (g_target_browser_source.empty()) {
			LOGW("Rebuilt artifacts but did not swap: no target Browser Source selected.");
		} else {
			LOGW("Rebuilt artifacts but did not swap: target Browser Source '%s' missing or not a Browser Source.",
			     g_target_browser_source.c_str());
		}
	}

	g_last_html_path = newHtml;
	return true;
}

void notify_list_updated(const std::string &id)
{
	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Update;
	l.id2 = id;
	l.count = (int64_t)g_items.size();
	emit_event(l);
}

bool reload_from_disk_and_rebuild()
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();

	const bool okState = load_state_json();
	const bool okVis   = load_visible_json();
	const bool okReb   = rebuild_and_swap();
	const bool ok      = okState && okVis && okReb;

	core_event r;
	r.type = event_type::Reloaded;
	r.ok = ok;
	r.count = (int64_t)g_items.size();
	emit_event(r);

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Reload;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return ok;
}

bool set_output_dir_and_load(const std::string &dir)
{
	if (dir.empty())
		return false;

	g_output_dir = dir;
	ensure_dir(output_dir());

	save_global_config();

	ensure_output_artifacts_exist();
	load_state_json();
	load_visible_json();

	save_state_json();
	save_visible_json();

	const bool ok = rebuild_and_swap();

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Reload;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return ok;
}

void init_from_disk()
{
	load_global_config();

	if (g_output_dir.empty())
		return;

	ensure_dir(output_dir());
	ensure_output_artifacts_exist();
	load_state_json();
	load_visible_json();

	// On startup, try to reuse the most recent generated HTML bundle.
	g_last_html_path = find_latest_lt_html();

	if (!g_last_html_path.empty() && file_exists(g_last_html_path)) {
		if (target_browser_source_exists()) {
			swap_target_browser_source_to_file(g_last_html_path);
		} else {
			if (!g_target_browser_source.empty()) {
				LOGW("Saved target Browser Source '%s' not found (startup swap skipped).",
				     g_target_browser_source.c_str());
			}
		}
	}
}

// -------------------------
// CRUD helpers (persist + notify list change)
// -------------------------

// -------------------------
// Carousel CRUD helpers (dock-only; persist + notify list change)
// -------------------------
std::string add_default_carousel()
{
	if (!has_output_dir())
		return {};

	ensure_output_artifacts_exist();
	load_state_json();

	carousel_cfg c;
	c.id = new_id();
	while (get_carousel_by_id(c.id))
		c.id = new_id();

	int maxOrder = -1;
	for (const auto &it : g_carousels)
		maxOrder = std::max(maxOrder, it.order);
	c.order = maxOrder + 1;

	c.title = "Carousel";
	c.order_mode = 0;
	c.loop = true;
	// Defaults per request
	c.visible_ms = 15000;
	c.interval_ms = 5000;
	c.dock_color = "#2EA043";

	g_carousels.push_back(c);
	std::sort(g_carousels.begin(), g_carousels.end(), [](const carousel_cfg &a, const carousel_cfg &b) {
		if (a.order != b.order)
			return a.order < b.order;
		return a.id < b.id;
	});

	save_state_json();

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Update;
	l.id = c.id;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return c.id;
}

bool update_carousel(const carousel_cfg &c)
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();
	load_state_json();

	carousel_cfg *dst = get_carousel_by_id(c.id);
	if (!dst)
		return false;

	*dst = c;
	if (dst->order_mode != 1)
		dst->order_mode = 0;
	// Any lower third that is part of a carousel should not use per-item repeat.
	for (const auto &mid : dst->members) {
		if (auto *lt = get_by_id(mid)) {
			lt->repeat_every_sec = 0;
			lt->repeat_visible_sec = 0;
		}
	}
	save_state_json();

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Update;
	l.id = c.id;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return true;
}

bool remove_carousel(const std::string &carousel_id)
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();
	load_state_json();

	const std::string sid = sanitize_id(carousel_id);
	const auto before = g_carousels.size();

	g_carousels.erase(std::remove_if(g_carousels.begin(), g_carousels.end(),
					 [&](const carousel_cfg &c) { return c.id == sid; }),
			 g_carousels.end());

	const bool removed = (g_carousels.size() != before);
	if (!removed)
		return false;

	save_state_json();

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Update;
	l.id = sid;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return true;
}

bool set_carousel_members(const std::string &carousel_id, const std::vector<std::string> &members)
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();
	load_state_json();

	carousel_cfg *c = get_carousel_by_id(carousel_id);
	if (!c)
		return false;

	c->members.clear();
	c->members.reserve(members.size());
	for (const auto &m : members) {
		const std::string mid = sanitize_id(m);
		if (mid.empty())
			continue;

		// A lower third may belong to only one carousel at a time.
		// If it is already claimed by a different carousel, do not move it implicitly.
		bool ownedByOther = false;
		for (const auto &other : g_carousels) {
			if (other.id == c->id)
				continue;
			if (std::find(other.members.begin(), other.members.end(), mid) != other.members.end()) {
				ownedByOther = true;
				break;
			}
		}
		if (ownedByOther)
			continue;

		c->members.push_back(mid);

		// When a lower third is added to a carousel, per-item repeat timers become redundant.
		// Enforce 0 to avoid confusion (dock-only setting; overlay behavior is driven by the carousel runner).
		if (auto *lt = get_by_id(mid)) {
			lt->repeat_every_sec = 0;
			lt->repeat_visible_sec = 0;
		}
	}

	save_state_json();

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Update;
	l.id = c->id;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return true;
}

std::string add_default_lower_third()
{
	if (!has_output_dir())
		return {};

	ensure_output_artifacts_exist();
	load_state_json();
	load_visible_json();

	lower_third_cfg c = default_cfg();
	while (get_by_id(c.id))
		c.id = new_id();

	int maxOrder = -1;
	for (const auto &it : g_items)
		maxOrder = std::max(maxOrder, it.order);
	c.order = maxOrder + 1;
	if (c.label.empty())
		c.label = c.title.empty() ? c.id : c.title;

	g_items.push_back(c);
	std::sort(g_items.begin(), g_items.end(), [](const lower_third_cfg &a, const lower_third_cfg &b) {
		if (a.order != b.order)
			return a.order < b.order;
		return a.id < b.id;
	});
	set_visible_nosave(c.id, true);

	if (!save_state_json())
		return {};
	save_visible_json();

	if (!rebuild_and_swap())
		return {};

	{
		core_event l;
		l.type = event_type::ListChanged;
		l.reason = list_change_reason::Create;
		l.id = c.id;
		l.count = (int64_t)g_items.size();
		emit_event(l);

		core_event v;
		v.type = event_type::VisibilityChanged;
		v.id = c.id;
		v.visible = true;
		v.visible_ids = visible_ids();
		emit_event(v);
	}

	return c.id;
}

std::string clone_lower_third(const std::string &id)
{
	if (!has_output_dir())
		return {};

	ensure_output_artifacts_exist();
	load_state_json();
	load_visible_json();

	const std::string sid = sanitize_id(id);
	lower_third_cfg *src = get_by_id(sid);
	if (!src)
		return {};

	lower_third_cfg c = *src;
	c.id = new_id();
	while (get_by_id(c.id))
		c.id = new_id();

	if (!c.title.empty())
		c.title += " (Copy)";
	else
		c.title = "Lower Third (Copy)";

	if (c.label.empty())
		c.label = c.title;
	else
		c.label += " (Copy)";

	// If the source lower third has an owned profile picture copied into output_dir,
	// clone it as well so each item owns its file lifecycle. Otherwise removing one
	// item could delete the image used by the other.
	if (!c.profile_picture.empty()) {
		const std::string srcRel = c.profile_picture;
		const std::string srcPath = output_dir() + "/" + srcRel;

		// Derive extension (if any)
		std::string ext;
		const auto dot = srcRel.find_last_of('.');
		if (dot != std::string::npos && dot + 1 < srcRel.size())
			ext = srcRel.substr(dot + 1);

		// New unique name based on new id + timestamp
		using namespace std::chrono;
		const auto ts = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		std::string newName = c.id + "_" + std::to_string((long long)ts);
		if (!ext.empty())
			newName += "." + ext;

		const std::string dstPath = output_dir() + "/" + newName;

		std::error_code ec;
		if (std::filesystem::exists(std::filesystem::path(srcPath), ec) && !ec) {
			ec.clear();
			std::filesystem::copy_file(std::filesystem::path(srcPath), std::filesystem::path(dstPath),
						  std::filesystem::copy_options::overwrite_existing, ec);
			if (!ec) {
				c.profile_picture = newName;
			} else {
				LOGW("clone_lower_third: failed to copy profile picture '%s' -> '%s' (%d)",
				     srcPath.c_str(), dstPath.c_str(), (int)ec.value());
				// Avoid sharing a file pointer to a different item's owned file.
				c.profile_picture.clear();
			}
		} else {
			// Source file missing; clear so the clone doesn't reference an invalid path.
			c.profile_picture.clear();
		}
	}

	int maxOrder = -1;
	for (const auto &it : g_items)
		maxOrder = std::max(maxOrder, it.order);
	c.order = maxOrder + 1;

	const std::string newId = c.id;

	g_items.push_back(c);
	std::sort(g_items.begin(), g_items.end(), [](const lower_third_cfg &a, const lower_third_cfg &b) {
		if (a.order != b.order)
			return a.order < b.order;
		return a.id < b.id;
	});
	set_visible_nosave(newId, true);

	if (!save_state_json())
		return {};
	save_visible_json();

	if (!rebuild_and_swap())
		return {};

	{
		core_event l;
		l.type = event_type::ListChanged;
		l.reason = list_change_reason::Clone;
		l.id = sid;
		l.id2 = newId;
		l.count = (int64_t)g_items.size();
		emit_event(l);

		core_event v;
		v.type = event_type::VisibilityChanged;
		v.id = newId;
		v.visible = true;
		v.visible_ids = visible_ids();
		emit_event(v);
	}

	return newId;
}

bool remove_lower_third(const std::string &id)
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();
	load_state_json();
	load_visible_json();

	const std::string sid = sanitize_id(id);
	const auto before = g_items.size();

	// Capture owned media before erasing so we can clean it up.
	std::string profileToDelete;
	std::string animInSoundToDelete;
	std::string animOutSoundToDelete;
	for (const auto &c : g_items) {
		if (c.id == sid) {
			profileToDelete = c.profile_picture;
			animInSoundToDelete = c.anim_in_sound;
			animOutSoundToDelete = c.anim_out_sound;
			break;
		}
	}

	const bool wasVisible = is_visible(sid);

	g_items.erase(std::remove_if(g_items.begin(), g_items.end(),
				     [&](const lower_third_cfg &c) { return c.id == sid; }),
		      g_items.end());

	const bool removed = (g_items.size() != before);
	if (!removed)
		return false;

	// Best-effort cleanup: remove the associated profile picture from disk.
	// (These are generated/copied into output_dir with unique names.)
	if (!profileToDelete.empty()) {
		const std::string fullPath = output_dir() + "/" + profileToDelete;
		std::error_code ec;
		(void)std::filesystem::remove(std::filesystem::path(fullPath), ec);
	}

	if (!animInSoundToDelete.empty()) {
		const std::string fullPath = output_dir() + "/" + animInSoundToDelete;
		std::error_code ec;
		(void)std::filesystem::remove(std::filesystem::path(fullPath), ec);
	}
	if (!animOutSoundToDelete.empty()) {
		const std::string fullPath = output_dir() + "/" + animOutSoundToDelete;
		std::error_code ec;
		(void)std::filesystem::remove(std::filesystem::path(fullPath), ec);
	}

	set_visible_nosave(sid, false);

	// Remove from any carousels
	for (auto &car : g_carousels) {
		car.members.erase(std::remove(car.members.begin(), car.members.end(), sid), car.members.end());
	}

	save_state_json();
	save_visible_json();

	const bool ok = rebuild_and_swap();

	{
		core_event l;
		l.type = event_type::ListChanged;
		l.reason = list_change_reason::Delete;
		l.id = sid;
		l.count = (int64_t)g_items.size();
		emit_event(l);

		if (wasVisible) {
			core_event v;
			v.type = event_type::VisibilityChanged;
			v.id = sid;
			v.visible = false;
			v.visible_ids = visible_ids();
			emit_event(v);
		}
	}

	return ok;
}

bool move_lower_third(const std::string &id, int delta)
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();
	load_state_json();
	load_visible_json();

	const std::string sid = sanitize_id(id);
	if (sid.empty())
		return false;

	if (g_items.size() < 2)
		return false;

	int idx = -1;
	for (int i = 0; i < (int)g_items.size(); ++i) {
		if (g_items[(size_t)i].id == sid) {
			idx = i;
			break;
		}
	}
	if (idx < 0)
		return false;

	const int newIdx = idx + delta;
	if (newIdx < 0 || newIdx >= (int)g_items.size())
		return false;

	std::swap(g_items[(size_t)idx], g_items[(size_t)newIdx]);
	for (int i = 0; i < (int)g_items.size(); ++i)
		g_items[(size_t)i].order = i;

	if (!save_state_json())
		return false;

	core_event l;
	l.type = event_type::ListChanged;
	l.reason = list_change_reason::Update;
	l.id = sid;
	l.count = (int64_t)g_items.size();
	emit_event(l);

	return true;
}

} // namespace smart_lt
