// hotkeys.cpp
#define LOG_TAG "[" PLUGIN_NAME "][hotkeys]"
#include "hotkeys.hpp"

#include "core.hpp"

#include <obs.h>
#include <obs-hotkey.h>

#include <QCoreApplication>
#include <QKeyCombination>
#include <QMetaObject>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vflow::hotkeys {

namespace {

enum class kind { item, group };

struct registration {
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	std::string description;
};

// UI thread only
std::unordered_map<std::string, registration> g_items;
std::unordered_map<std::string, registration> g_groups;
trigger_fn g_onItem;
trigger_fn g_onGroup;
uint64_t g_listener = 0;
bool g_active = false;

// Read from the libobs hotkey thread
std::mutex g_lookupMx;
std::unordered_map<obs_hotkey_id, std::pair<kind, std::string>> g_lookup;

struct key_pair {
	int qt;
	obs_key_t obs;
};

// Keys whose meaning does not depend on the keyboard layout's shift state.
const key_pair kKeys[] = {
	{Qt::Key_A, OBS_KEY_A},
	{Qt::Key_B, OBS_KEY_B},
	{Qt::Key_C, OBS_KEY_C},
	{Qt::Key_D, OBS_KEY_D},
	{Qt::Key_E, OBS_KEY_E},
	{Qt::Key_F, OBS_KEY_F},
	{Qt::Key_G, OBS_KEY_G},
	{Qt::Key_H, OBS_KEY_H},
	{Qt::Key_I, OBS_KEY_I},
	{Qt::Key_J, OBS_KEY_J},
	{Qt::Key_K, OBS_KEY_K},
	{Qt::Key_L, OBS_KEY_L},
	{Qt::Key_M, OBS_KEY_M},
	{Qt::Key_N, OBS_KEY_N},
	{Qt::Key_O, OBS_KEY_O},
	{Qt::Key_P, OBS_KEY_P},
	{Qt::Key_Q, OBS_KEY_Q},
	{Qt::Key_R, OBS_KEY_R},
	{Qt::Key_S, OBS_KEY_S},
	{Qt::Key_T, OBS_KEY_T},
	{Qt::Key_U, OBS_KEY_U},
	{Qt::Key_V, OBS_KEY_V},
	{Qt::Key_W, OBS_KEY_W},
	{Qt::Key_X, OBS_KEY_X},
	{Qt::Key_Y, OBS_KEY_Y},
	{Qt::Key_Z, OBS_KEY_Z},
	{Qt::Key_0, OBS_KEY_0},
	{Qt::Key_1, OBS_KEY_1},
	{Qt::Key_2, OBS_KEY_2},
	{Qt::Key_3, OBS_KEY_3},
	{Qt::Key_4, OBS_KEY_4},
	{Qt::Key_5, OBS_KEY_5},
	{Qt::Key_6, OBS_KEY_6},
	{Qt::Key_7, OBS_KEY_7},
	{Qt::Key_8, OBS_KEY_8},
	{Qt::Key_9, OBS_KEY_9},
	{Qt::Key_F1, OBS_KEY_F1},
	{Qt::Key_F2, OBS_KEY_F2},
	{Qt::Key_F3, OBS_KEY_F3},
	{Qt::Key_F4, OBS_KEY_F4},
	{Qt::Key_F5, OBS_KEY_F5},
	{Qt::Key_F6, OBS_KEY_F6},
	{Qt::Key_F7, OBS_KEY_F7},
	{Qt::Key_F8, OBS_KEY_F8},
	{Qt::Key_F9, OBS_KEY_F9},
	{Qt::Key_F10, OBS_KEY_F10},
	{Qt::Key_F11, OBS_KEY_F11},
	{Qt::Key_F12, OBS_KEY_F12},
	{Qt::Key_F13, OBS_KEY_F13},
	{Qt::Key_F14, OBS_KEY_F14},
	{Qt::Key_F15, OBS_KEY_F15},
	{Qt::Key_F16, OBS_KEY_F16},
	{Qt::Key_F17, OBS_KEY_F17},
	{Qt::Key_F18, OBS_KEY_F18},
	{Qt::Key_F19, OBS_KEY_F19},
	{Qt::Key_F20, OBS_KEY_F20},
	{Qt::Key_F21, OBS_KEY_F21},
	{Qt::Key_F22, OBS_KEY_F22},
	{Qt::Key_F23, OBS_KEY_F23},
	{Qt::Key_F24, OBS_KEY_F24},
	{Qt::Key_Space, OBS_KEY_SPACE},
	{Qt::Key_Return, OBS_KEY_RETURN},
	{Qt::Key_Escape, OBS_KEY_ESCAPE},
	{Qt::Key_Tab, OBS_KEY_TAB},
	{Qt::Key_Backspace, OBS_KEY_BACKSPACE},
	{Qt::Key_Insert, OBS_KEY_INSERT},
	{Qt::Key_Delete, OBS_KEY_DELETE},
	{Qt::Key_Pause, OBS_KEY_PAUSE},
	{Qt::Key_Print, OBS_KEY_PRINT},
	{Qt::Key_Home, OBS_KEY_HOME},
	{Qt::Key_End, OBS_KEY_END},
	{Qt::Key_Left, OBS_KEY_LEFT},
	{Qt::Key_Up, OBS_KEY_UP},
	{Qt::Key_Right, OBS_KEY_RIGHT},
	{Qt::Key_Down, OBS_KEY_DOWN},
	{Qt::Key_PageUp, OBS_KEY_PAGEUP},
	{Qt::Key_PageDown, OBS_KEY_PAGEDOWN},
	{Qt::Key_NumLock, OBS_KEY_NUMLOCK},
	{Qt::Key_ScrollLock, OBS_KEY_SCROLLLOCK},
	{Qt::Key_Menu, OBS_KEY_MENU},
	{Qt::Key_Minus, OBS_KEY_MINUS},
	{Qt::Key_Equal, OBS_KEY_EQUAL},
	{Qt::Key_Comma, OBS_KEY_COMMA},
	{Qt::Key_Period, OBS_KEY_PERIOD},
	{Qt::Key_Slash, OBS_KEY_SLASH},
	{Qt::Key_Semicolon, OBS_KEY_SEMICOLON},
	{Qt::Key_Apostrophe, OBS_KEY_APOSTROPHE},
	{Qt::Key_BracketLeft, OBS_KEY_BRACKETLEFT},
	{Qt::Key_BracketRight, OBS_KEY_BRACKETRIGHT},
	{Qt::Key_Backslash, OBS_KEY_BACKSLASH},
	{Qt::Key_QuoteLeft, OBS_KEY_QUOTELEFT},
};

const key_pair kKeypad[] = {
	{Qt::Key_0, OBS_KEY_NUM0},
	{Qt::Key_1, OBS_KEY_NUM1},
	{Qt::Key_2, OBS_KEY_NUM2},
	{Qt::Key_3, OBS_KEY_NUM3},
	{Qt::Key_4, OBS_KEY_NUM4},
	{Qt::Key_5, OBS_KEY_NUM5},
	{Qt::Key_6, OBS_KEY_NUM6},
	{Qt::Key_7, OBS_KEY_NUM7},
	{Qt::Key_8, OBS_KEY_NUM8},
	{Qt::Key_9, OBS_KEY_NUM9},
	{Qt::Key_Asterisk, OBS_KEY_NUMASTERISK},
	{Qt::Key_Plus, OBS_KEY_NUMPLUS},
	{Qt::Key_Minus, OBS_KEY_NUMMINUS},
	{Qt::Key_Period, OBS_KEY_NUMPERIOD},
	{Qt::Key_Slash, OBS_KEY_NUMSLASH},
	{Qt::Key_Enter, OBS_KEY_ENTER},
};

// On macOS Qt reports Command as ControlModifier and Control as MetaModifier.
#if defined(__APPLE__)
constexpr uint32_t kQtCtrl = INTERACT_COMMAND_KEY;
constexpr uint32_t kQtMeta = INTERACT_CONTROL_KEY;
#else
constexpr uint32_t kQtCtrl = INTERACT_CONTROL_KEY;
constexpr uint32_t kQtMeta = INTERACT_COMMAND_KEY;
#endif

bool to_combo(const QKeySequence &seq, obs_key_combination_t &out)
{
	if (seq.isEmpty())
		return false;

	const QKeyCombination kc = seq[0];
	const Qt::KeyboardModifiers mods = kc.keyboardModifiers();
	const int key = kc.key();

	out.modifiers = 0;
	out.key = OBS_KEY_NONE;

	if (mods & Qt::KeypadModifier) {
		for (const auto &p : kKeypad) {
			if (p.qt == key) {
				out.key = p.obs;
				break;
			}
		}
	}
	if (out.key == OBS_KEY_NONE) {
		for (const auto &p : kKeys) {
			if (p.qt == key) {
				out.key = p.obs;
				break;
			}
		}
	}
	if (out.key == OBS_KEY_NONE)
		return false;

	if (mods & Qt::ShiftModifier)
		out.modifiers |= INTERACT_SHIFT_KEY;
	if (mods & Qt::ControlModifier)
		out.modifiers |= kQtCtrl;
	if (mods & Qt::AltModifier)
		out.modifiers |= INTERACT_ALT_KEY;
	if (mods & Qt::MetaModifier)
		out.modifiers |= kQtMeta;
	return true;
}

QKeySequence from_combo(const obs_key_combination_t &c)
{
	int key = 0;
	Qt::KeyboardModifiers mods;

	for (const auto &p : kKeypad) {
		if (p.obs == c.key) {
			key = p.qt;
			mods |= Qt::KeypadModifier;
			break;
		}
	}
	if (!key) {
		for (const auto &p : kKeys) {
			if (p.obs == c.key) {
				key = p.qt;
				break;
			}
		}
	}
	if (!key)
		return {};

	if (c.modifiers & INTERACT_SHIFT_KEY)
		mods |= Qt::ShiftModifier;
	if (c.modifiers & kQtCtrl)
		mods |= Qt::ControlModifier;
	if (c.modifiers & INTERACT_ALT_KEY)
		mods |= Qt::AltModifier;
	if (c.modifiers & kQtMeta)
		mods |= Qt::MetaModifier;

	return QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key)));
}

void on_hotkey(void *, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	std::pair<kind, std::string> target;
	{
		std::lock_guard<std::mutex> lk(g_lookupMx);
		auto it = g_lookup.find(id);
		if (it == g_lookup.end())
			return;
		target = it->second;
	}

	// Called on the libobs hotkey thread; all plugin state lives on the UI thread.
	QMetaObject::invokeMethod(
		QCoreApplication::instance(),
		[target]() {
			if (!g_active)
				return;
			const trigger_fn &fn = target.first == kind::item ? g_onItem : g_onGroup;
			if (fn)
				fn(QString::fromStdString(target.second));
		},
		Qt::QueuedConnection);
}

void load_bindings(obs_hotkey_id hid, const std::string &json, const std::string &legacy)
{
	if (!json.empty()) {
		if (obs_data_t *d = obs_data_create_from_json(json.c_str())) {
			obs_data_array_t *arr = obs_data_get_array(d, "bindings");
			if (arr) {
				obs_hotkey_load(hid, arr);
				obs_data_array_release(arr);
			} else {
				obs_hotkey_load_bindings(hid, nullptr, 0);
			}
			obs_data_release(d);
			return;
		}
	}

	// Migrate pre-4.0 shortcuts, which were stored as Qt key sequences.
	obs_key_combination_t combo{};
	const QKeySequence seq = QKeySequence::fromString(QString::fromStdString(legacy), QKeySequence::PortableText);
	if (!legacy.empty() && to_combo(seq, combo)) {
		obs_hotkey_load_bindings(hid, &combo, 1);
		return;
	}
	if (!legacy.empty())
		LOGW("Shortcut '%s' cannot be used as an OBS hotkey; please assign it in Settings > Hotkeys",
		     legacy.c_str());
	obs_hotkey_load_bindings(hid, nullptr, 0);
}

// True when the hotkey currently has at least one binding.
bool has_bindings(obs_hotkey_id hid)
{
	obs_data_array_t *arr = obs_hotkey_save(hid);
	const bool any = arr && obs_data_array_count(arr) > 0;
	obs_data_array_release(arr);
	return any;
}

std::string save_bindings(obs_hotkey_id hid)
{
	obs_data_array_t *arr = obs_hotkey_save(hid);
	obs_data_t *d = obs_data_create();
	if (arr)
		obs_data_set_array(d, "bindings", arr);
	std::string out = obs_data_get_json(d);
	obs_data_release(d);
	obs_data_array_release(arr);
	return out;
}

QKeySequence first_sequence(obs_hotkey_id hid)
{
	struct query {
		obs_hotkey_id id;
		bool found = false;
		obs_key_combination_t combo{};
	} q{hid};

	obs_enum_hotkey_bindings(
		[](void *data, size_t, obs_hotkey_binding_t *binding) {
			auto *q = static_cast<query *>(data);
			if (obs_hotkey_binding_get_hotkey_id(binding) != q->id)
				return true;
			q->combo = obs_hotkey_binding_get_key_combination(binding);
			q->found = true;
			return false;
		},
		&q);

	return q.found ? from_combo(q.combo) : QKeySequence();
}

std::string describe_item(const lower_third_cfg &c)
{
	const std::string name = !c.label.empty() ? c.label : (!c.title.empty() ? c.title : c.id);
	return "VinciFlow: Show/Hide \"" + name + "\"";
}

std::string describe_group(const group_cfg &g)
{
	const std::string name = !g.title.empty() ? g.title : g.id;
	return "VinciFlow: Start/Stop group \"" + name + "\"";
}

registration register_hotkey(kind k, const std::string &id, const std::string &desc)
{
	const std::string name = (k == kind::item ? "vinciflow.lt." : "vinciflow.group.") + id;
	registration r;
	r.id = obs_hotkey_register_frontend(name.c_str(), desc.c_str(), on_hotkey, nullptr);
	r.description = desc;
	if (r.id != OBS_INVALID_HOTKEY_ID) {
		std::lock_guard<std::mutex> lk(g_lookupMx);
		g_lookup[r.id] = {k, id};
	}
	return r;
}

void unregister_hotkey(const registration &r)
{
	if (r.id == OBS_INVALID_HOTKEY_ID)
		return;
	{
		std::lock_guard<std::mutex> lk(g_lookupMx);
		g_lookup.erase(r.id);
	}
	obs_hotkey_unregister(r.id);
}

// Generic reconciliation between configs and registrations.
template<typename Cfg, typename Desc, typename Bindings, typename Legacy>
void sync_list(kind k, std::vector<Cfg> &cfgs, std::unordered_map<std::string, registration> &regs, bool reload,
	       Desc describe, Bindings bindings, Legacy legacy)
{
	std::unordered_set<std::string> alive;
	for (auto &c : cfgs) {
		alive.insert(c.id);
		const std::string desc = describe(c);
		auto it = regs.find(c.id);
		if (it == regs.end()) {
			registration r = register_hotkey(k, c.id, desc);
			if (r.id == OBS_INVALID_HOTKEY_ID)
				continue;
			load_bindings(r.id, bindings(c), legacy(c));
			regs.emplace(c.id, r);
			continue;
		}
		if (reload)
			load_bindings(it->second.id, bindings(c), legacy(c));
		if (it->second.description != desc) {
			obs_hotkey_set_description(it->second.id, desc.c_str());
			it->second.description = desc;
		}
	}

	for (auto it = regs.begin(); it != regs.end();) {
		if (alive.count(it->first)) {
			++it;
			continue;
		}
		unregister_hotkey(it->second);
		it = regs.erase(it);
	}
}

void sync(bool reload)
{
	sync_list(
		kind::item, vflow::all(), g_items, reload, describe_item,
		[](const lower_third_cfg &c) { return c.hotkey_bindings; },
		[](const lower_third_cfg &c) { return c.hotkey; });
	sync_list(
		kind::group, vflow::groups(), g_groups, reload, describe_group,
		[](const group_cfg &g) { return g.toggle_hotkey_bindings; },
		[](const group_cfg &g) { return g.toggle_hotkey; });
}

// Before-save hook: copies the live OBS bindings (which may have been changed in
// Settings > Hotkeys) into the configs that are about to be written.
void capture_all()
{
	for (auto &c : vflow::all()) {
		auto it = g_items.find(c.id);
		if (it == g_items.end())
			continue;
		// Keep an unmigratable pre-4.0 shortcut text until the user binds something.
		if (c.hotkey_bindings.empty() && !c.hotkey.empty() && !has_bindings(it->second.id))
			continue;
		c.hotkey_bindings = save_bindings(it->second.id);
		c.hotkey = first_sequence(it->second.id).toString(QKeySequence::PortableText).toStdString();
	}
	for (auto &g : vflow::groups()) {
		auto it = g_groups.find(g.id);
		if (it == g_groups.end())
			continue;
		if (g.toggle_hotkey_bindings.empty() && !g.toggle_hotkey.empty() && !has_bindings(it->second.id))
			continue;
		g.toggle_hotkey_bindings = save_bindings(it->second.id);
		g.toggle_hotkey = first_sequence(it->second.id).toString(QKeySequence::PortableText).toStdString();
	}
}

void on_core_event(const core_event &ev, void *)
{
	if (ev.type == event_type::Reloaded)
		sync(true);
	else if (ev.type == event_type::ListChanged)
		sync(ev.reason == list_change_reason::Reload);
}

bool apply_sequence(const std::unordered_map<std::string, registration> &regs, const QString &id,
		    const QKeySequence &seq)
{
	auto it = regs.find(id.toStdString());
	if (it == regs.end())
		return false;

	if (seq.isEmpty()) {
		obs_hotkey_load_bindings(it->second.id, nullptr, 0);
		return true;
	}

	obs_key_combination_t combo{};
	if (!to_combo(seq, combo))
		return false;
	obs_hotkey_load_bindings(it->second.id, &combo, 1);
	return true;
}

QKeySequence lookup_sequence(const std::unordered_map<std::string, registration> &regs, const QString &id)
{
	auto it = regs.find(id.toStdString());
	return it == regs.end() ? QKeySequence() : first_sequence(it->second.id);
}

} // namespace

void init(trigger_fn onItem, trigger_fn onGroup)
{
	g_onItem = std::move(onItem);
	g_onGroup = std::move(onGroup);
	g_active = true;

	sync(false);
	vflow::set_before_save_hook(&capture_all);
	if (!g_listener)
		g_listener = vflow::add_event_listener(&on_core_event, nullptr);

	LOGI("Registered %zu lower third and %zu group hotkeys", g_items.size(), g_groups.size());
}

void shutdown()
{
	if (!g_active)
		return;

	g_active = false;
	vflow::set_before_save_hook(nullptr);
	if (g_listener) {
		vflow::remove_event_listener(g_listener);
		g_listener = 0;
	}

	for (auto &kv : g_items)
		unregister_hotkey(kv.second);
	for (auto &kv : g_groups)
		unregister_hotkey(kv.second);
	g_items.clear();
	g_groups.clear();
	g_onItem = nullptr;
	g_onGroup = nullptr;
}

QKeySequence item_sequence(const QString &id)
{
	return lookup_sequence(g_items, id);
}

QKeySequence group_sequence(const QString &id)
{
	return lookup_sequence(g_groups, id);
}

bool set_item_sequence(const QString &id, const QKeySequence &seq)
{
	if (!apply_sequence(g_items, id, seq))
		return false;
	if (auto *c = vflow::get_by_id(id.toStdString()))
		c->hotkey = seq.toString(QKeySequence::PortableText).toStdString();
	return true;
}

bool set_group_sequence(const QString &id, const QKeySequence &seq)
{
	if (!apply_sequence(g_groups, id, seq))
		return false;
	if (auto *g = vflow::get_group_by_id(id.toStdString()))
		g->toggle_hotkey = seq.toString(QKeySequence::PortableText).toStdString();
	return true;
}

bool is_supported(const QKeySequence &seq)
{
	obs_key_combination_t combo{};
	return seq.isEmpty() || to_combo(seq, combo);
}

} // namespace vflow::hotkeys
