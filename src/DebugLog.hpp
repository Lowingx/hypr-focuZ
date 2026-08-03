#pragma once

#include <string>

// Minimal file logger used to diagnose session freezes. Gated by the
// FOCUSZ_DEBUG env var or the plugin:focusZ:debug config value: when off, every
// call is a no-op and no thread is spawned, so the hot path stays untouched in
// production. Logs to ~/.local/share/hyprland/focusz-debug.log (survives reboot).
//
// While enabled, a background thread writes a heartbeat line every 200ms that
// also samples the Hyprland main thread's CPU time + kernel state via /proc.
// - If the compositor main thread deadlocks, the heartbeat keeps ticking and
//   the last logged handler line (with no matching "exit") pinpoints the hang.
// - If the main thread busy-loops (e.g. a render feedback loop), the heartbeat
//   shows main cpu_delta climbing with state=R while handler logs stop.
namespace DebugLog {
    void init();          // call once from PLUGIN_INIT (on the main thread); enables if FOCUSZ_DEBUG env set
    void shutdown();      // call once from PLUGIN_EXIT
    void setEnabled(bool); // runtime toggle (e.g. from the plugin:focusZ:debug config value)
    bool isEnabled();
    void log(const std::string& msg); // timestamped, thread-safe append
}
