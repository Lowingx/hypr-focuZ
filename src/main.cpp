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

// Singleton manager
UP<CDepthFocusManager> g_pDepthFocusManager;

// Event listeners (must be stored to keep them alive)
// Each .listen() call returns a SP<CSignalListener> — we just need to keep them alive
static SP<CSignalListener> g_focusListener;
static SP<CSignalListener> g_openListener;
static SP<CSignalListener> g_closeListener;
static SP<CSignalListener> g_destroyListener;
static SP<CSignalListener> g_renderListener;

// ---------------------------------------------------------
// Required plugin API exports
// ---------------------------------------------------------

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    DebugLog::init();

    // --- Version check ---
    // Compare the running server's API hash against the hash baked into the
    // headers this plugin was compiled with. getHyprlandVersion().hash is only
    // the git commit, while __hyprland_api_get_hash() is the full dependency
    // hash string — comparing those two would always fail.
    const std::string hash       = __hyprland_api_get_hash();
    const std::string clientHash = __hyprland_api_get_client_hash();

    if (hash != clientHash) {
        HyprlandAPI::addNotification(PHANDLE,
            "[focusZ] Version mismatch — plugin compiled for a different Hyprland build. Unloading.",
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 15000);
        throw std::runtime_error("focusZ: version mismatch | server=" + hash + " | client=" + clientHash);
    }

    // --- Register config values using makeConfigValue ---
    using namespace Config::Values;

    g_bEnabled       = makeConfigValue<CBoolValue>("plugin:focusZ:enabled",
                          "Enable or disable the Z-axis depth focus effect", true, {});
    g_bStacking      = makeConfigValue<CBoolValue>("plugin:focusZ:stacking",
                          "Stack windows as overlapping cards (floats them); disable to keep tiling", true, {});
    g_iMaxLayers     = makeConfigValue<CIntValue>("plugin:focusZ:max_layers",
                          "Number of distinct depth levels (1-16); windows beyond still join the deck at the deepest level", 8,
                          Config::Values::SIntValueOptions{.min = 1, .max = 16});
    g_fLayer1Scale   = makeConfigValue<CFloatValue>("plugin:focusZ:layer_1_scale",
                          "Scale factor for background layer 1 (0.4–1.0)", 0.62f,
                          Config::Values::SFloatValueOptions{.min = 0.4f, .max = 1.0f});
    g_fLayer2Scale   = makeConfigValue<CFloatValue>("plugin:focusZ:layer_2_scale",
                          "Scale factor for background layer 2 (0.3–1.0)", 0.42f,
                          Config::Values::SFloatValueOptions{.min = 0.3f, .max = 1.0f});
    g_fLayer1Opacity = makeConfigValue<CFloatValue>("plugin:focusZ:layer_1_opacity",
                          "Opacity for background layer 1 (0.1–1.0)", 0.5f,
                          Config::Values::SFloatValueOptions{.min = 0.1f, .max = 1.0f});
    g_fLayer2Opacity = makeConfigValue<CFloatValue>("plugin:focusZ:layer_2_opacity",
                          "Opacity for background layer 2 (0.1–1.0)", 0.2f,
                          Config::Values::SFloatValueOptions{.min = 0.1f, .max = 1.0f});
    g_bLayer1Blur    = makeConfigValue<CBoolValue>("plugin:focusZ:layer_1_blur",
                          "Enable blur on background layer 1", true, {});
    g_bLayer2Blur    = makeConfigValue<CBoolValue>("plugin:focusZ:layer_2_blur",
                          "Enable blur on background layer 2", true, {});
    g_fAnimationSpeed = makeConfigValue<CFloatValue>("plugin:focusZ:animation_speed",
                          "Animation speed for depth transitions (1.0–20.0)", 8.0f,
                          Config::Values::SFloatValueOptions{.min = 1.0f, .max = 20.0f});
    g_fWallpaperDim   = makeConfigValue<CFloatValue>("plugin:focusZ:wallpaper_dim",
                          "Fade alpha of the wallpaper while the deck is live (0.1 = very dark canvas, 1.0 = unchanged)", 0.5f,
                          Config::Values::SFloatValueOptions{.min = 0.1f, .max = 1.0f});
    g_bCenterScale   = makeConfigValue<CBoolValue>("plugin:focusZ:center_scale",
                          "Scale windows toward the center of the monitor", true, {});
    g_bDebug         = makeConfigValue<CBoolValue>("plugin:focusZ:debug",
                          "Write debug logs + heartbeat to /tmp/focusz-debug.log", false, {});

    // Register all config values with Hyprland
    bool ok = true;
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_bEnabled);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_bStacking);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_iMaxLayers);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_fLayer1Scale);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_fLayer2Scale);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_fLayer1Opacity);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_fLayer2Opacity);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_bLayer1Blur);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_bLayer2Blur);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_fAnimationSpeed);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_fWallpaperDim);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_bCenterScale);
    ok &= HyprlandAPI::addConfigValueV2(PHANDLE, g_bDebug);

    if (!ok) {
        HyprlandAPI::addNotification(PHANDLE,
            "[focusZ] Failed to register some config values.",
            CHyprColor{1.0, 0.5, 0.2, 1.0}, 8000);
    }

    // Sync debug logger with the plugin:focusZ:debug config value (or FOCUSZ_DEBUG env).
    DebugLog::setEnabled(g_bDebug && g_bDebug->value());

    // --- Create the depth focus manager ---
    g_pDepthFocusManager = Hyprutils::Memory::makeUnique<CDepthFocusManager>();

    // --- Register EventBus listeners ---

    // Focus change
    g_focusListener = Event::bus()->m_events.window.active.listen(
        [](PHLWINDOW w, Desktop::eFocusReason reason) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onFocusChange(w, reason);
        });

    // Window open
    g_openListener = Event::bus()->m_events.window.open.listen(
        [](PHLWINDOW w) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onWindowOpen(w);
        });

    // Window close
    g_closeListener = Event::bus()->m_events.window.close.listen(
        [](PHLWINDOW w) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onWindowClose(w);
        });

    // Window destroy
    g_destroyListener = Event::bus()->m_events.window.destroy.listen(
        [](PHLWINDOWREF w) {
            auto pw = w.lock();
            if (g_pDepthFocusManager && valid(pw))
                g_pDepthFocusManager->onWindowClose(pw);
        });

    // Render stage hook
    g_renderListener = Event::bus()->m_events.render.stage.listen(
        [](eRenderStage stage) {
            if (g_pDepthFocusManager)
                g_pDepthFocusManager->onRenderStage(stage);
        });

    DebugLog::log("focusZ listeners registered");

    // --- Register dispatcher for keybind cycling ---
    HyprlandAPI::addDispatcherV2(PHANDLE, "focusZ:cycle",
        [](std::string) -> SDispatchResult {
            if (!g_pDepthFocusManager)
                return {.passEvent = false, .success = false, .error = "focusZ not initialized"};

            // Delegate to Hyprland's built-in window cycling
            HyprlandAPI::invokeHyprctlCommand("dispatch", "cyclenext");
            return {.passEvent = false, .success = true, .error = ""};
        });

    // --- Notification ---
    HyprlandAPI::addNotification(PHANDLE,
        "[focusZ] Plugin loaded — Z-Axis Depth Focus Layout active.",
        CHyprColor{0.2, 0.8, 0.4, 1.0}, 4000);

    DebugLog::log("focusZ plugin init done");

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
    g_renderListener.reset();

    g_pDepthFocusManager.reset();

    HyprlandAPI::addNotification(PHANDLE,
        "[focusZ] Plugin unloaded.",
        CHyprColor{0.5, 0.5, 0.5, 1.0}, 3000);

    DebugLog::shutdown();
}
