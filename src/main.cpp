#include "globals.hpp"
#include "DepthFocus.hpp"
#include "DebugLog.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/SharedDefs.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string clientHashFromHeaders() {
    auto stripPatch = [](const char* ver) -> std::string {
        std::string_view v = ver;
        if (!v.contains('.'))
            return std::string{v};
        return std::string{v.substr(0, v.find_last_of('.'))};
    };
    return std::string{GIT_COMMIT_HASH} + "_aq_" + stripPatch(AQUAMARINE_VERSION) + "_hu_" + stripPatch(HYPRUTILS_VERSION) + "_hg_" +
           stripPatch(HYPRGRAPHICS_VERSION) + "_hc_" + stripPatch(HYPRCURSOR_VERSION) + "_hlg_" + stripPatch(HYPRLANG_VERSION);
}

} // namespace

// Singleton manager
UP<CDepthFocusManager> g_pDepthFocusManager;

// Event listeners (must be stored to keep them alive)
// Each .listen() call returns a SP<CSignalListener> — we just need to keep them alive
static SP<CSignalListener> g_focusListener;
static SP<CSignalListener> g_openListener;
static SP<CSignalListener> g_closeListener;
static SP<CSignalListener> g_destroyListener;

// ---------------------------------------------------------
// Required plugin API exports
// ---------------------------------------------------------

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    DebugLog::init();

    DebugLog::log("PLUGIN_INIT start");

    // --- Version check ---
    // Compare the running server's API hash against the hash baked into the
    // headers this plugin was compiled with. getHyprlandVersion().hash is only
    // the git commit, while __hyprland_api_get_hash() is the full dependency
    // hash string — comparing those two would always fail.
    //
    // The client hash is computed locally from the version macros rather than
    // via the header's exported __hyprland_api_get_client_hash(): that symbol is
    // inline and interposable, so a stale copy of this plugin mapped earlier in
    // the process (e.g. after a rebuild while the session kept running) can
    // shadow it and report a hash from an old Hyprland build.
    const std::string hash       = __hyprland_api_get_hash();
    const std::string clientHash = clientHashFromHeaders();

    if (hash != clientHash) {
        HyprlandAPI::addNotification(PHANDLE,
            "[focusZ] Version mismatch — plugin compiled for a different Hyprland build. Unloading.",
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 15000);
        throw std::runtime_error("focusZ: version mismatch | server=" + hash + " | client=" + clientHash);
    }

    // --- Register config values using the new addConfigValueV2 API ---
    // This registers keys in Hyprland's config system so hl.config() from lua can set them.
    // Access via cfg().xxx->value() at runtime.

    cfg().enabled        = makeShared<Config::Values::CBoolValue>("plugin:focusZ:enabled", "enable or disable the plugin", true);
    cfg().stacking       = makeShared<Config::Values::CBoolValue>("plugin:focusZ:stacking", "float the deck as overlapping cards", true);
    cfg().maxLayers      = makeShared<Config::Values::CIntValue>("plugin:focusZ:max_layers", "maximum depth layers", 8, Config::Values::SIntValueOptions{.min = 1, .max = 16});
    cfg().layer1Scale    = makeShared<Config::Values::CFloatValue>("plugin:focusZ:layer_1_scale", "scale of the first back card", 0.70F, Config::Values::SFloatValueOptions{.min = 0.1F, .max = 1.0F});
    cfg().layer2Scale    = makeShared<Config::Values::CFloatValue>("plugin:focusZ:layer_2_scale", "scale of the second back card", 0.50F, Config::Values::SFloatValueOptions{.min = 0.1F, .max = 1.0F});
    cfg().layer1Opacity  = makeShared<Config::Values::CFloatValue>("plugin:focusZ:layer_1_opacity", "opacity of the first back card", 0.85F, Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    cfg().layer2Opacity  = makeShared<Config::Values::CFloatValue>("plugin:focusZ:layer_2_opacity", "opacity of the second back card", 0.70F, Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    cfg().frontScale    = makeShared<Config::Values::CFloatValue>("plugin:focusZ:card_front_scale", "front card scale", 0.72F, Config::Values::SFloatValueOptions{.min = 0.3F, .max = 1.0F});
    cfg().edgeScatter   = makeShared<Config::Values::CBoolValue>("plugin:focusZ:card_edge_scatter", "scatter cards near edges", true);
    cfg().scatterReshuffle = makeShared<Config::Values::CBoolValue>("plugin:focusZ:card_scatter_reshuffle", "reshuffle positions on rebuild", true);
    cfg().peekMin       = makeShared<Config::Values::CFloatValue>("plugin:focusZ:card_peek_min", "minimum peek strip", 24.0F, Config::Values::SFloatValueOptions{.min = 0.0F, .max = 200.0F});
    cfg().peekMax       = makeShared<Config::Values::CFloatValue>("plugin:focusZ:card_peek_max", "maximum peek strip", 80.0F, Config::Values::SFloatValueOptions{.min = 0.0F, .max = 400.0F});
    cfg().debug         = makeShared<Config::Values::CBoolValue>("plugin:focusZ:debug", "enable debug logging", false);

    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().enabled);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().stacking);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().maxLayers);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().layer1Scale);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().layer2Scale);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().layer1Opacity);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().layer2Opacity);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().frontScale);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().edgeScatter);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().scatterReshuffle);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().peekMin);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().peekMax);
    HyprlandAPI::addConfigValueV2(PHANDLE, cfg().debug);

    DebugLog::log("config registration done");

    // Sync debug logger with FOCUSZ_DEBUG env or config key (config values not ready during PLUGIN_INIT).
    DebugLog::setEnabled(std::getenv("FOCUSZ_DEBUG") != nullptr || cfg().debug->value());

    // --- Create the depth focus manager ---
    g_pDepthFocusManager = Hyprutils::Memory::makeUnique<CDepthFocusManager>();

    // --- Register EventBus listeners ---
    // Only window lifecycle events (no render path, no decorations, no transformers).

    g_focusListener = Event::bus()->m_events.window.active.listen(
        [](PHLWINDOW w, Desktop::eFocusReason reason) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onFocusChange(w, reason);
        });

    g_openListener = Event::bus()->m_events.window.open.listen(
        [](PHLWINDOW w) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onWindowOpen(w);
        });

    g_closeListener = Event::bus()->m_events.window.close.listen(
        [](PHLWINDOW w) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onWindowClose(w);
        });

    g_destroyListener = Event::bus()->m_events.window.destroy.listen(
        [](PHLWINDOWREF w) {
            auto pw = w.lock();
            if (g_pDepthFocusManager && valid(pw))
                g_pDepthFocusManager->onWindowClose(pw);
        });

    DebugLog::log("focusZ listeners registered");

    // --- Register dispatcher for keybind cycling ---
    HyprlandAPI::addDispatcherV2(PHANDLE, "focusZ:cycle",
        [](std::string) -> SDispatchResult {
            if (!g_pDepthFocusManager)
                return {.passEvent = false, .success = false, .error = "focusZ not initialized"};
            HyprlandAPI::invokeHyprctlCommand("dispatch", "cyclenext");
            return {.passEvent = false, .success = true, .error = ""};
        });

    HyprlandAPI::addNotification(PHANDLE,
        "[focusZ] Plugin loaded (FULLY ENABLED).",
        CHyprColor{0.2, 0.8, 0.4, 1.0}, 4000);

    return {"focusZ",
            "Z-Axis Depth Focus Layout — 3D stacking visual effect based on focus depth",
            "Lowingx",
            "1.0.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_focusListener.reset();
    g_openListener.reset();
    g_closeListener.reset();
    g_destroyListener.reset();

    g_pDepthFocusManager.reset();

    HyprlandAPI::addNotification(PHANDLE,
        "[focusZ] Plugin unloaded.",
        CHyprColor{0.5, 0.5, 0.5, 1.0}, 3000);

    DebugLog::shutdown();
}
