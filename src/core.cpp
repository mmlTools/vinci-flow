// core.cpp
#define LOG_TAG "[" PLUGIN_NAME "][core]"
#include "core.hpp"

#include <algorithm>
#include <sstream>
#include <random>
#include <cctype>
#include <mutex>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <obs.h>
#include <obs-module.h>

namespace smart_lt {

// -------------------------
// Globals
// -------------------------
static std::string g_output_dir;
static std::string g_target_browser_source;
static std::vector<lower_third_cfg> g_items;
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
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	f.write(data.data(), (qint64)data.size());
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

// -------------------------
// Defaults
// -------------------------
static lower_third_cfg default_cfg()
{
	lower_third_cfg c;
	c.id = new_id();
	c.title = "New Lower Third";
	c.subtitle = "Subtitle";
	c.profile_picture.clear();

	c.anim_in = "animate__fadeInUp";
	c.anim_out = "animate__fadeOutDown";
	c.custom_anim_in.clear();
	c.custom_anim_out.clear();

	c.font_family = "Inter";
	c.lt_position = "lt-pos-bottom-left";

	c.bg_color = "#111827";
	c.text_color = "#F9FAFB";
	c.opacity = 85;
	c.radius = 5;

	c.html_template =
		R"HTML(
<div class="slt-card">
  <div class="slt-left">
    <img class="slt-avatar" src="{{PROFILE_PICTURE_URL}}" alt="" onerror="this.style.display='none'">
  </div>
  <div class="slt-right">
    <div class="slt-title">{{TITLE}}</div>
    <div class="slt-subtitle">{{SUBTITLE}}</div>
  </div>
</div>
)HTML";

	c.css_template =
		R"CSS(
.slt-card {
  display: flex; align-items: center; gap: 12px;
  padding: 14px 18px;
  border-radius: {{RADIUS}}%;
  background: {{BG_COLOR}};
  color: {{TEXT_COLOR}};
  box-shadow: 0 10px 30px rgba(0,0,0,0.35);
  font-family: {{FONT_FAMILY}}, system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
}

.slt-avatar {
  width: 100px; height: 100px; border-radius: 50%;
  object-fit: cover; background: rgba(255,255,255,0.08);
  flex-shrink: 0;
}

.slt-avatar:not([src]),
.slt-avatar[src=""],
.slt-avatar[src="./"] {
  display: none !important;
}

.slt-title { font-weight: 700; font-size: 46px; line-height: 1.1; }
.slt-subtitle { opacity: 0.9; font-size: 24px; margin-top: 2px; }
)CSS";

	c.js_template = "// Custom JS logic here";
	c.repeat_every_sec = 0;
	c.repeat_visible_sec = 3;
	c.hotkey.clear();
	return c;
}

static std::string resolve_in_class(const lower_third_cfg &c)
{
	if (c.anim_in == "custom")
		return c.custom_anim_in;
	return c.anim_in;
}

static std::string resolve_out_class(const lower_third_cfg &c)
{
	if (c.anim_out == "custom")
		return c.custom_anim_out;
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
	css = replace_all(css, "{{BG_COLOR}}", c.bg_color);
	css = replace_all(css, "{{OPACITY}}", std::to_string(c.opacity));
	css = replace_all(css, "{{RADIUS}}", std::to_string(c.radius));
	css = replace_all(css, "{{TEXT_COLOR}}", c.text_color);
	css = replace_all(css, "{{FONT_FAMILY}}", c.font_family.empty() ? "Inter" : c.font_family);

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
	std::string map = "{\n";
	for (const auto &c : items) {
		const std::string inC = resolve_in_class(c);
		const std::string outC = resolve_out_class(c);
		int delay = 0;

		map += "    \"" + c.id +
		       "\": { "
		       "inCls: " +
		       (inC.empty() ? "null" : ("\"" + inC + "\"")) +
		       ", outCls: " + (outC.empty() ? "null" : ("\"" + outC + "\"")) +
		       ", delay: " + std::to_string(delay) + " },\n";
	}
	map += "  };\n";

	return std::string(R"JS(
/* Smart Lower Thirds – Animation Script (lifecycle + race-safe) */
(() => {
  const VISIBLE_URL = "./lt-visible.json";
  const animMap = )JS") +
	       map + R"JS(

  // Safety bounds (avoid deadlocks)
  const MAX_HOOK_WAIT_MS = 1200;
  const MAX_ANIM_WAIT_MS = 1600;

  function stripAnimate(el) {
    el.classList.remove("animate__animated");
    el.style.animationDelay = "";
    el.style.animationDuration = "";
    el.style.animationTimingFunction = "";
    [...el.classList].forEach(c => {
      if (c.startsWith("animate__")) el.classList.remove(c);
    });
  }

  function hasAnim(cls) {
    return cls && String(cls).trim().length > 0;
  }

  function getHook(el, name) {
    const fn = el && el[name];
    return (typeof fn === "function") ? fn : null;
  }

  function markWant(el, shouldShow) {
    el.dataset.want = shouldShow ? "1" : "0";
  }

  function wantsShow(el) { return el.dataset.want === "1"; }
  function wantsHide(el) { return el.dataset.want === "0"; }

  function nextOp(el) {
    el.__slt_op = (el.__slt_op || 0) + 1;
    return el.__slt_op;
  }

  function ensureCurrent(el, op) {
    if ((el.__slt_op || 0) !== op) throw new Error("superseded");
  }

  async function runHook(el, name, op) {
    const fn = getHook(el, name);
    if (!fn) return;

    try {
      const r = fn();
      if (r && typeof r.then === "function") {
        await Promise.race([
          r,
          new Promise(res => setTimeout(res, MAX_HOOK_WAIT_MS))
        ]);
      }
    } catch (e) {}

    ensureCurrent(el, op);
  }

  function waitForOwnAnimationEnd(el, op) {
    return new Promise(resolve => {
      let done = false;

      const cleanup = () => {
        if (done) return;
        done = true;
        el.removeEventListener("animationend", onEnd, true);
      };

      const onEnd = (ev) => {
        // Only end when the <li> itself ends its animation (ignore child animation events)
        if (ev.target !== el) return;
        cleanup();
        resolve(true);
      };

      // Capture = true so we still receive it even if user scripts stop propagation
      el.addEventListener("animationend", onEnd, true);

      // Failsafe (e.g., missing animate.css or browser quirks)
      setTimeout(() => {
        if (!done) {
          cleanup();
          resolve(true);
        }
      }, MAX_ANIM_WAIT_MS);
    }).then(() => {
      ensureCurrent(el, op);
      return true;
    });
  }

  async function applyIn(el, cfg) {
    const op = nextOp(el);

    // If intent changed already, abort.
    if (!wantsShow(el)) return;

    el.dataset.state = "showing";

    // Cancel any in-flight out animation visually and force displayed
    stripAnimate(el);
    el.classList.remove("slt-hidden");
    el.classList.add("slt-visible");

    if (hasAnim(cfg.inCls)) {
      if (cfg.delay > 0) el.style.animationDelay = cfg.delay + "ms";

      el.classList.add("animate__animated");
      cfg.inCls.split(/\s+/).forEach(c => el.classList.add(c));

      await waitForOwnAnimationEnd(el, op);

      // Might have been superseded or intent flipped
      if (!wantsShow(el)) return;

      el.dataset.state = "visible";
      el.style.animationDelay = "";
    } else {
      el.dataset.state = "visible";
    }

    // Lifecycle: after shown (template may run inner sequencing)
    await runHook(el, "__slt_onShown", op);

    // Final sanity: don't force visible if user toggled hide during hook
    if (!wantsShow(el)) return;

    el.dataset.state = "visible";
    el.classList.remove("slt-hidden");
    el.classList.add("slt-visible");
  }

  async function applyOut(el, cfg) {
    const op = nextOp(el);

    // If intent changed already, abort.
    if (!wantsHide(el)) return;

    // Lifecycle: template can animate inner exit BEFORE parent out anim
    el.dataset.state = "hiding_pending";
    await runHook(el, "__slt_beforeHide", op);

    // If user toggled back to show while we waited, abort hide.
    if (!wantsHide(el)) return;

    el.dataset.state = "hiding";

    // Ensure we start parent out cleanly (remove any in classes)
    stripAnimate(el);

    if (hasAnim(cfg.outCls)) {
      el.classList.add("animate__animated");
      cfg.outCls.split(/\s+/).forEach(c => el.classList.add(c));

      await waitForOwnAnimationEnd(el, op);

      // If user toggled show during parent out, abort final hide.
      if (!wantsHide(el)) return;

      stripAnimate(el);
      el.classList.remove("slt-visible");
      el.classList.add("slt-hidden");
      el.dataset.state = "hidden";
    } else {
      el.classList.remove("slt-visible");
      el.classList.add("slt-hidden");
      el.dataset.state = "hidden";
    }
  }

  async function tick() {
    try {
      const r = await fetch(VISIBLE_URL + "?t=" + Date.now(), { cache: "no-store" });
      const visibleIds = await r.json();
      if (!Array.isArray(visibleIds)) return;

      const visibleSet = new Set(visibleIds.map(String));
      const els = Array.from(document.querySelectorAll("#slt-root > li[id]"));

      // Pass 1: update intent for all elements (prevents per-element races)
      for (const el of els) {
        markWant(el, visibleSet.has(el.id));
      }

      // Pass 2: drive state machine (fire-and-forget; op token makes it safe)
      for (const el of els) {
        const cfg = animMap[el.id] || {};
        const shouldShow = wantsShow(el);
        const state = el.dataset.state || "hidden";

        if (shouldShow) {
          if (state !== "visible" && state !== "showing") {
            applyIn(el, cfg);
          }
        } else {
          if (state !== "hidden" && state !== "hiding" && state !== "hiding_pending") {
            applyOut(el, cfg);
          }
        }
      }
    } catch (e) {}
  }

  document.addEventListener("DOMContentLoaded", () => {
    tick();
    setInterval(tick, 350);
  });
})();
)JS";
}

static std::string build_item_script(const lower_third_cfg &c)
{
	std::string js = c.js_template;
	js = replace_all(js, "{{ID}}", c.id);

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

static std::string build_full_html()
{
	std::string html;
	html += "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\"/>\n";
	html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n";
	html += "<link rel=\"stylesheet\" href=\"./lt-styles.css\"/>\n";

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
		inner = replace_all(inner, "{{BG_COLOR}}", c.bg_color);
		inner = replace_all(inner, "{{OPACITY}}", std::to_string(c.opacity));
		inner = replace_all(inner, "{{RADIUS}}", std::to_string(c.radius));
		inner = replace_all(inner, "{{TEXT_COLOR}}", c.text_color);
		inner = replace_all(inner, "{{FONT_FAMILY}}", c.font_family.empty() ? "Inter" : c.font_family);

		const std::string pic = c.profile_picture.empty() ? "./" : ("./" + c.profile_picture);
		inner = replace_all(inner, "{{PROFILE_PICTURE_URL}}", pic);

		if (inner.find("onerror") == std::string::npos) {
			inner = replace_all(inner, "<img ", "<img onerror=\"this.style.display='none'\" ");
		}

		html += "  <li id=\"" + c.id + "\" class=\"" + c.lt_position + "\">";
		html += inner;
		html += "</li>\n";
	}

	html += "</ul>\n<script src=\"./lt-scripts.js\"></script>\n</body>\n</html>\n";
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
	return has_output_dir() ? join_path(g_output_dir, "lt-styles.css") : "";
}

std::string path_scripts_js()
{
	return has_output_dir() ? join_path(g_output_dir, "lt-scripts.js") : "";
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

	// keep in sync with existing items only
	if (!get_by_id(id))
		return false;

	const bool before = is_visible(id);
	if (before == visible) {
		return true; // no-op
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
		return true;
	}

	const std::string txt = read_text_file(p);
	if (txt.empty()) {
		g_items.clear();
		return true;
	}

	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(txt), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		LOGW("Invalid lt-state.json; reset");
		g_items.clear();
		return false;
	}

	const QJsonObject root = doc.object();
	const QJsonArray items = root.value("items").toArray();

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

		c.title = o.value("title").toString().toStdString();
		c.subtitle = o.value("subtitle").toString().toStdString();
		c.profile_picture = o.value("profile_picture").toString().toStdString();

		c.anim_in = o.value("anim_in").toString().toStdString();
		c.anim_out = o.value("anim_out").toString().toStdString();
		c.custom_anim_in = o.value("custom_anim_in").toString().toStdString();
		c.custom_anim_out = o.value("custom_anim_out").toString().toStdString();

		c.font_family = o.value("font_family").toString().toStdString();
		c.lt_position = o.value("lt_position").toString().toStdString();

		c.bg_color = o.value("bg_color").toString().toStdString();
		c.text_color = o.value("text_color").toString().toStdString();
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
		if (c.bg_color.empty())
			c.bg_color = "#111827";
		if (c.text_color.empty())
			c.text_color = "#F9FAFB";

		out.push_back(std::move(c));
	}

	g_items = std::move(out);
	return true;
}

bool save_state_json()
{
	if (!has_output_dir())
		return false;

	QJsonObject root;
	root["version"] = 1;

	QJsonArray items;
	for (const auto &c : g_items) {
		QJsonObject o;
		o["id"] = QString::fromStdString(c.id);
		o["title"] = QString::fromStdString(c.title);
		o["subtitle"] = QString::fromStdString(c.subtitle);
		o["profile_picture"] = QString::fromStdString(c.profile_picture);

		o["anim_in"] = QString::fromStdString(c.anim_in);
		o["anim_out"] = QString::fromStdString(c.anim_out);
		o["custom_anim_in"] = QString::fromStdString(c.custom_anim_in);
		o["custom_anim_out"] = QString::fromStdString(c.custom_anim_out);

		o["font_family"] = QString::fromStdString(c.font_family);
		o["lt_position"] = QString::fromStdString(c.lt_position);

		o["bg_color"] = QString::fromStdString(c.bg_color);
		o["text_color"] = QString::fromStdString(c.text_color);
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

bool regenerate_merged_css_js()
{
	if (!has_output_dir())
		return false;

	std::string css;
	css += build_shared_css();

	css += R"CSS(

/* Position classes */
.lt-pos-bottom-left  { left: var(--slt-safe-margin); bottom: var(--slt-safe-margin); }
.lt-pos-bottom-right { right: var(--slt-safe-margin); bottom: var(--slt-safe-margin); }
.lt-pos-top-left     { left: var(--slt-safe-margin); top: var(--slt-safe-margin); }
.lt-pos-top-right    { right: var(--slt-safe-margin); top: var(--slt-safe-margin); }
.lt-pos-center       { left: 50%; top: 50%; transform: translate(-50%, -50%); }
)CSS";

	css += "\n/* Per-LT scoped styles */\n";
	for (const auto &c : g_items)
		css += "\n" + scope_css_best_effort(c);

	if (!write_text_file(path_styles_css(), css)) {
		LOGW("Failed writing lt-styles.css");
		return false;
	}

	std::string js;
	js += build_base_script(g_items);
	js += "\n\n/* Per-LT scripts */\n";
	for (const auto &c : g_items)
		js += build_item_script(c);

	if (!write_text_file(path_scripts_js(), js)) {
		LOGW("Failed writing lt-scripts.js");
		return false;
	}

	return true;
}

std::string generate_timestamp_html()
{
	if (!has_output_dir())
		return {};

	const std::string fn = "lt-" + now_timestamp_string() + ".html";
	const std::string abs = join_path(output_dir(), fn);
	if (!write_text_file(abs, build_full_html()))
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

	// Returns with refcount +1
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

bool target_browser_source_exists()
{
	obs_source_t *src = get_target_browser_source();
	if (!src)
		return false;

	// Defensive: ensure the selection is still a Browser Source
	const char *id = obs_source_get_id(src);
	const bool ok = (id && std::string(id) == sltBrowserSourceId);

	obs_source_release(src);
	return ok;
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

	// Defensive: ensure it's still a browser source
	const char *id = obs_source_get_id(src);
	if (!id || std::string(id) != sltBrowserSourceId) {
		LOGW("Target source '%s' is not a Browser Source.", g_target_browser_source.c_str());
		obs_source_release(src);
		return false;
	}

	obs_data_t *s = obs_source_get_settings(src);
	obs_data_set_bool(s, "is_local_file", true);
	obs_data_set_string(s, "local_file", absoluteHtmlPath.c_str());
	obs_data_set_bool(s, "smart_lt_managed", true);
	obs_source_update(src, s);

	obs_data_release(s);
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

	if (!regenerate_merged_css_js())
		return false;

	const std::string newHtml = generate_timestamp_html();
	if (newHtml.empty())
		return false;

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

	delete_old_lt_html_keep(newHtml);
	g_last_html_path = newHtml;
	return true;
}

// Force reload state+visible from disk and rebuild/swap (with notifications)
bool reload_from_disk_and_rebuild()
{
	if (!has_output_dir())
		return false;

	ensure_output_artifacts_exist();

	const bool okState = load_state_json();
	const bool okVis   = load_visible_json();
	const bool okReb   = rebuild_and_swap();
	const bool ok      = okState && okVis && okReb;

	// Emit Reloaded + ListChanged(reason=Reload)
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

	// Treat as reload for listeners
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

	g_last_html_path = find_latest_lt_html();
	if (!g_last_html_path.empty()) {
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

	g_items.push_back(c);
	set_visible_nosave(c.id, true);

	if (!save_state_json())
		return {};
	save_visible_json();

	if (!rebuild_and_swap())
		return {};

	// notify list changed + visibility changed (optional but useful)
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

	const std::string newId = c.id;

	g_items.push_back(c);
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

	const bool wasVisible = is_visible(sid);

	g_items.erase(std::remove_if(g_items.begin(), g_items.end(),
				     [&](const lower_third_cfg &c) { return c.id == sid; }),
		      g_items.end());

	const bool removed = (g_items.size() != before);
	if (!removed)
		return false;

	set_visible_nosave(sid, false);

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

} // namespace smart_lt