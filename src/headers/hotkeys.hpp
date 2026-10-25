#pragma once

#include <functional>

#include <QKeySequence>
#include <QString>

// OBS-native hotkeys for lower thirds and groups.
//
// Every lower third gets a "Show/Hide" hotkey and every group a "Start/Stop"
// hotkey, registered with libobs. They work while OBS is in the background,
// appear in Settings > Hotkeys, and are persisted in lt-state.json. All functions
// must be called on the UI thread; triggers are delivered on the UI thread.
namespace vflow::hotkeys {

using trigger_fn = std::function<void(const QString &id)>;

// Registers hotkeys for the current state and keeps them in sync with core events.
void init(trigger_fn onItem, trigger_fn onGroup);
void shutdown();

// Shortcut shown/edited in the plugin dialogs (first OBS binding).
// An empty sequence means "no shortcut".
QKeySequence item_sequence(const QString &id);
QKeySequence group_sequence(const QString &id);

// Replaces the OBS bindings of the hotkey with the given sequence (first chord).
// Returns false if the shortcut cannot be expressed as an OBS hotkey.
bool set_item_sequence(const QString &id, const QKeySequence &seq);
bool set_group_sequence(const QString &id, const QKeySequence &seq);

// True when the key sequence can be represented by an OBS hotkey.
bool is_supported(const QKeySequence &seq);

} // namespace vflow::hotkeys
