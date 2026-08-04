#include "DepthFocus.hpp"
#include "DepthShadow.hpp"
#include "DebugLog.hpp"
#include "ScaleTransformer.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <algorithm>
#include <cmath>

static std::string winStr(PHLWINDOW pWindow) {
    return pWindow ? "0x" + std::to_string((uintptr_t)pWindow.get()) : "(null)";
}

// The scale transformer attached to a window, or nullptr.
static CScaleTransformer* getScaleTransformer(PHLWINDOW pWindow) {
    for (const auto& up : pWindow->m_transformers) {
        if (auto* t = dynamic_cast<CScaleTransformer*>(up.get()))
            return t;
    }
    return nullptr;
}

// Attach/update/remove the scale transformer for a window. scale >= ~1 means
// "no transform": the transformer is dropped so the window keeps rendering on
// the direct (cheaper) path. Attaching one routes the window through the
// official transformed-render pipeline (separate fb + transform() + blit back).
static void setWindowScale(PHLWINDOW pWindow, float scale) {
    if (scale >= 0.995f) {
        std::erase_if(pWindow->m_transformers, [](const auto& up) {
            return dynamic_cast<CScaleTransformer*>(up.get()) != nullptr;
        });
        return;
    }
    if (auto* t = getScaleTransformer(pWindow)) {
        t->setScale(scale);
        return;
    }
    pWindow->m_transformers.push_back(Hyprutils::Memory::makeUnique<CScaleTransformer>(pWindow, scale));
}

// Sync the debug logger with the plugin:focusZ:debug config value (runtime toggle).
static void syncDebug() {
    DebugLog::setEnabled(g_bDebug && g_bDebug->value());
}

CDepthFocusManager::CDepthFocusManager() {
    init();
}

SLayerTransform CDepthFocusManager::getTransformForLayer(int depth) const {
    SLayerTransform t;
    if (depth <= 0) {
        // Layer 0: focused window, full scale
        t.scale   = 1.0f;
        t.opacity = 1.0f;
    } else if (depth == 1) {
        t.scale   = g_fLayer1Scale  ? g_fLayer1Scale->value()  : 0.85f;
        t.opacity = g_fLayer1Opacity ? g_fLayer1Opacity->value() : 0.7f;
    } else if (depth == 2) {
        t.scale   = g_fLayer2Scale  ? g_fLayer2Scale->value()  : 0.70f;
        t.opacity = g_fLayer2Opacity ? g_fLayer2Opacity->value() : 0.4f;
    } else {
        // Deeper layers: extrapolate from layer 2 with a steeper per-layer
        // falloff so the Z-perspective reads strongly.
        const float baseScale   = g_fLayer2Scale  ? g_fLayer2Scale->value()  : 0.70f;
        const float baseOpacity = g_fLayer2Opacity ? g_fLayer2Opacity->value() : 0.4f;
        const int   extraDepth  = depth - 2;
        t.scale   = std::max(0.22f, baseScale - 0.13f * extraDepth);
        t.opacity = std::max(0.05f, baseOpacity - 0.18f * extraDepth);
    }
    return t;
}

int CDepthFocusManager::getLayerDepth(PHLWINDOW pWindow) const {
    if (!valid(pWindow))
        return -1;

    // O(1) hot path (render hook). The cache is rebuilt on every stack mutation.
    auto it = m_depthCache.find((uintptr_t)pWindow.get());
    if (it != m_depthCache.end())
        return it->second;

    // Fallback scan: covers windows mapped between events.
    for (size_t i = 0; i < m_zStack.size(); i++) {
        if (m_zStack[i].lock() == pWindow)
            return (int)i;
    }
    return -1;
}

void CDepthFocusManager::init() {
    rebuildStack();
}

void CDepthFocusManager::rebuildStack() {
    DebugLog::log("rebuildStack enter");

    // Disabled: unwind every window we touched and drop all state.
    if (g_bEnabled && !g_bEnabled->value()) {
        for (const auto& ref : m_zStack) {
            auto w = ref.lock();
            if (valid(w))
                restoreWindow(w);
        }
        pullWallpaper(false);
        m_zStack.clear();
        m_applied.clear();
        m_depthCache.clear();
        m_monitor = {};
        return;
    }

    const auto& windows = Desktop::windowState()->windows();
    const auto focused  = Desktop::focusState()->window();

    // Anchor the stack to the focused window's monitor: background layers stay
    // isolated per workspace/monitor, so windows on other monitors are untouched.
    if (valid(focused))
        m_monitor = focused->m_monitor;
    const auto anchor = m_monitor.lock();

    std::vector<PHLWINDOW> next;
    if (valid(focused))
        next.push_back(focused);

    for (const auto& w : windows) {
        if (!w->m_isMapped || w->isHidden())
            continue;
        if (w == focused)
            continue;
        if (anchor && !(w->m_monitor.lock() == anchor))
            continue;
        // Only the anchor monitor's active workspace joins the stack; windows on
        // other workspaces of the same monitor must not be floated/repositioned.
        if (anchor && w->m_workspace && w->m_workspace != anchor->m_activeWorkspace)
            continue;
        next.push_back(w);
    }

    // The deck holds *every* mapped window on the anchor workspace: deep layers
    // floor at scale/opacity 0.22/0.05, so windows beyond max_layers still join
    // as dim ghosts instead of snapping back to 100% behind the deck. A generous
    // hard cap only guards pathological workspaces.
    constexpr int HARD_CAP = 32;
    if ((int)next.size() > HARD_CAP)
        next.resize(HARD_CAP);

    m_zStack.clear();
    for (auto& w : next)
        m_zStack.emplace_back(w);

    DebugLog::log("rebuildStack stack_size=" + std::to_string(m_zStack.size()) + " monitor=" +
                  (m_monitor.lock() ? "set" : "none"));

    applyAllDepthTransforms();
    DebugLog::log("rebuildStack exit");
}

void CDepthFocusManager::onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason /*reason*/) {
    syncDebug();
    if (g_bEnabled && !g_bEnabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    DebugLog::log("onFocusChange win=" + winStr(pWindow));

    // Cross-monitor (or first) focus: re-anchor the whole stack.
    if (!m_monitor.lock() || !(pWindow->m_monitor.lock() == m_monitor.lock())) {
        rebuildStack();
        DebugLog::log("onFocusChange exit (rebuild)");
        return;
    }

    promoteWindow(pWindow);
    applyAllDepthTransforms();
    DebugLog::log("onFocusChange exit");
}

void CDepthFocusManager::onWindowOpen(PHLWINDOW pWindow) {
    syncDebug();
    if (g_bEnabled && !g_bEnabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    DebugLog::log("onWindowOpen win=" + winStr(pWindow));

    // Only windows on the anchor monitor join this stack.
    if (m_monitor.lock() && !(pWindow->m_monitor.lock() == m_monitor.lock()))
        return;

    // ... and only on the anchor workspace (matches rebuildStack's filter).
    const auto anchor = m_monitor.lock();
    if (anchor && pWindow->m_workspace && pWindow->m_workspace != anchor->m_activeWorkspace)
        return;

    promoteWindow(pWindow);
    applyAllDepthTransforms();
    DebugLog::log("onWindowOpen exit");
}

void CDepthFocusManager::onWindowClose(PHLWINDOW pWindow) {
    syncDebug();
    if (g_bEnabled && !g_bEnabled->value())
        return;

    bool found = false;
    for (auto it = m_zStack.begin(); it != m_zStack.end();) {
        if (it->lock() == pWindow) {
            it = m_zStack.erase(it);
            found = true;
        } else {
            ++it;
        }
    }

    if (!found)
        return;

    DebugLog::log("onWindowClose win=" + winStr(pWindow) + " stack_after=" + std::to_string(m_zStack.size()));

    // The window is gone; drop tracking so restore never touches a dead window.
    m_applied.erase((uintptr_t)pWindow.get());

    if (m_zStack.empty())
        m_monitor = {};

    applyAllDepthTransforms();
    DebugLog::log("onWindowClose exit");
}

void CDepthFocusManager::promoteWindow(PHLWINDOW pWindow) {
    // Remove from current position if it exists
    for (auto it = m_zStack.begin(); it != m_zStack.end();) {
        if (it->lock() == pWindow)
            it = m_zStack.erase(it);
        else
            ++it;
    }

    // Insert at front (Layer 0). Like rebuildStack, the deck is not truncated to
    // max_layers — every window joins, deeper ones flooring their transform.
    m_zStack.insert(m_zStack.begin(), pWindow);
    constexpr int HARD_CAP = 32;
    if ((int)m_zStack.size() > HARD_CAP)
        m_zStack.resize(HARD_CAP);

    DebugLog::log("promoteWindow win=" + winStr(pWindow) + " stack_size=" + std::to_string(m_zStack.size()));
}

void CDepthFocusManager::applyAllDepthTransforms() {
    // stacking toggled since last apply: switch modes (float+scale boxes ↔
    // tiled+content-scale), not just re-apply the same windows.
    const bool stackingNow = g_bStacking ? g_bStacking->value() : true;
    if (stackingNow != m_stackingActive) {
        const bool turningOff = m_stackingActive && !stackingNow;
        m_stackingActive      = stackingNow;
        if (turningOff) {
            for (const auto& ref : m_zStack) {
                auto w = ref.lock();
                if (valid(w))
                    restoreWindow(w);
            }
            pullWallpaper(false);
            m_zStack.clear();
            m_applied.clear();
            m_depthCache.clear();
            m_monitor = {};
            DebugLog::log("applyAllDepthTransforms stacking off — stack cleared");
            return;
        }
        rebuildStack();
        return;
    }

    refreshDepthCache();

    // Restore windows that left the stack (moved monitors, closed).
    for (auto it = m_applied.begin(); it != m_applied.end();) {
        auto w = it->second.window.lock();
        if (valid(w) && m_depthCache.contains(it->first)) {
            ++it;
            continue;
        }
        if (valid(w))
            restoreWindow(w);
        it = m_applied.erase(it);
    }

    // Apply only what changed; unchanged windows keep their goals and are not re-damaged.
    // Depth is clamped to max_layers so windows beyond the configured levels all
    // share the deepest treatment (their transforms floor at 0.22/0.05 anyway).
    const int maxLayers = std::max<int>(1, g_iMaxLayers ? g_iMaxLayers->value() : 8);
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;

        const uintptr_t addr = (uintptr_t)w.get();
        auto            it   = m_applied.find(addr);
        const int       depth = std::min<int>((int)i, maxLayers - 1);
        if (it != m_applied.end() && it->second.depth == depth)
            continue;

        applyDepthToWindow(w, depth);
    }

    // Arrange the stack as overlapping floating cards (focused on top).
    layoutStack();

    DebugLog::log("applyAllDepthTransforms exit (applied=" + std::to_string(m_applied.size()) +
                  " stack=" + std::to_string(m_zStack.size()) + ")");
}

void CDepthFocusManager::applyDepthToWindow(PHLWINDOW pWindow, int depth) {
    if (!valid(pWindow))
        return;

    const uintptr_t addr = (uintptr_t)pWindow.get();
    const auto      transform = getTransformForLayer(depth);

    auto& state = m_applied[addr];
    state.window = pWindow;
    state.depth  = depth;

    // A stint as a back card pins the window's box (front × layer scale); the
    // next time it is promoted to front, layoutStack re-applies the default
    // front box (see frontBoxSet).
    if (depth != 0)
        state.frontBoxSet = false;

    DebugLog::log("applyDepthToWindow win=" + winStr(pWindow) + " depth=" + std::to_string(depth) +
                  " opacity=" + std::to_string(transform.opacity) + " scale=" + std::to_string(transform.scale));

    // Opacity is a pure render property — safe on tiled and floating windows alike.
    auto& alphaVar = pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE);
    *alphaVar = transform.opacity;

    // Scale: with stacking ON the window's *box* carries the scale (layoutStack
    // shrinks deeper cards), so no content transformer — otherwise the content
    // would be double-scaled (box × render). With stacking OFF (tiled fallback)
    // geometry can't change, so the scale is routed through the
    // Render::IWindowTransformer (the only render-safe per-window transform in
    // Hyprland 0.56.1). Depth 0 (focused) and stacked windows drop the
    // transformer to stay on the cheap direct path.
    // NOTE: renderModif / RMOD_TYPE_SCALECENTER is never used — see the gotcha
    // in onRenderStage; and geometry is never touched for tiled windows
    // (sizeAnim/posAnim fought the layout engine and froze the session).
    const bool stacking = g_bStacking && g_bStacking->value();
    setWindowScale(pWindow, stacking ? 1.0f : transform.scale);

    // Depth-varying shadow decoration: background windows only.
    if (depth > 0) {
        if (!state.decorated) {
            HyprlandAPI::addWindowDecoration(PHANDLE, pWindow,
                Hyprutils::Memory::makeUnique<CDepthShadowDecoration>(pWindow, depth));
            state.decorated = true;
        } else {
            for (const auto& deco : pWindow->m_windowDecorations) {
                auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
                if (shadowDeco)
                    shadowDeco->setDepth(depth);
            }
        }
    } else if (state.decorated) {
        for (const auto& deco : pWindow->m_windowDecorations) {
            auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
            if (shadowDeco) {
                HyprlandAPI::removeWindowDecoration(PHANDLE, shadowDeco);
                break;
            }
        }
        state.decorated = false;
    }

    // Damage the window to ensure re-render (only reached when the transform changed).
    g_pHyprRenderer->damageWindow(pWindow, true);
}

void CDepthFocusManager::pullWallpaper(bool pull) {
    auto anchor = m_monitor.lock();

    // No anchor (stack cleared/disabled): nothing to pull, forget any stale ref.
    if (!anchor) {
        m_wallpaper       = {};
        m_wallpaperDimmed = false;
        return;
    }

    // Locate the wallpaper: the first mapped surface on the BACKGROUND layer.
    // The namespace varies per wallpaper daemon (swww/hyprpaper/mpvpaper), so we
    // match by layer instead of name.
    if (!m_wallpaper.lock()) {
        for (const auto& lsl : anchor->m_layerSurfaceLayers[0]) {
            auto ls = lsl.lock();
            if (valid(ls) && ls->visible()) {
                m_wallpaper = ls;
                break;
            }
        }
    }

    auto ls = m_wallpaper.lock();
    if (!valid(ls) || !ls->visible()) {
        m_wallpaper       = {};
        m_wallpaperDimmed = false;
        return;
    }

    if (m_wallpaperDimmed == pull)
        return; // already in the desired state

    // Only the fade alpha channel exists for layer surfaces; it's how the whole
    // layer is dimmed, which reads as the wallpaper receding along the Z axis.
    // The dim is configurable (plugin:focusZ:wallpaper_dim) so the wallpaper can
    // act as a darker "canvas" for the deck; geometry is NOT touched (see above).
    const float dim = g_fWallpaperDim ? g_fWallpaperDim->value() : 0.5f; // 1.0 = full, lower = pushed back
    *ls->alpha().get(Desktop::View::LS_ALPHA_FADE) = pull ? dim : 1.0f;
    m_wallpaperDimmed = pull;

    DebugLog::log(std::string("pullWallpaper ") + (pull ? "pull" : "restore"));
}

void CDepthFocusManager::layoutStack() {
    // Stacking off (or not yet initialized): stay tiled and push the wallpaper back.
    if (g_bStacking && !g_bStacking->value()) {
        pullWallpaper(false);
        return;
    }

    const auto anchor = m_monitor.lock();
    if (!anchor || m_zStack.empty()) {
        pullWallpaper(false);
        return;
    }

    // Base card = workarea minus a margin; the focused card keeps that box. Back
    // cards are *smaller* (box scaled by the layer's scale factor) and each one
    // is scattered at a random angle/radius around the front card, pushed
    // PEEK_MIN..PEEK_MAX px past its footprint, so every card protrudes and stays
    // visible behind the front one — the depth reads as receding scale +
    // translucency + blur, and the scatter is random but never hidden. Offsets
    // are hashed from the window address, so positions never jitter across focus
    // changes.
    constexpr double MARGIN      = 48.0;  // front card's inset from the workarea
    constexpr double FRONT_SCALE = 0.70;  // front card is 70% of the base box
    constexpr double PEEK_MIN    = 20.0;  // min protrude beyond the front card (px)
    constexpr double PEEK_MAX    = 40.0;  // max protrude (px) — keep < MARGIN so it stays on-screen

    // Deterministic splitmix64 finalizer from a uintptr_t seed.
    auto randSeed = [](uintptr_t seed) -> uint64_t {
        uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        return h;
    };

    CBox base = anchor->logicalBoxMinusReserved();
    base      = CBox{base.x + MARGIN, base.y + MARGIN, base.w - 2 * MARGIN, base.h - 2 * MARGIN};

    // The front card starts at FRONT_SCALE of the base box, centered; while it
    // stays focused the user owns its box (resize/drag) and back cards anchor to
    // its *live* geometry, so the deck always tracks the focused card.
    CBox frontBox = CBox{base.x + base.w * (1.0 - FRONT_SCALE) / 2.0,
                         base.y + base.h * (1.0 - FRONT_SCALE) / 2.0,
                         base.w * FRONT_SCALE,
                         base.h * FRONT_SCALE};

    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;

        // Fullscreen windows keep Hyprland's fullscreen geometry.
        if (Fullscreen::controller()->isFullscreen(w))
            continue;

        const uintptr_t addr = (uintptr_t)w.get();
        auto&           st   = m_applied[addr];

        // Floating is required for us to own the box: updatePos() then applies
        // our m_box directly, instead of the tiling algorithm re-arranging the
        // window every frame (the geometry-freeze loop from AGENTS.md).
        if (!w->m_isFloating) {
            if (!st.floatingManaged) {
                st.floatingBefore  = false;
                st.floatingManaged = true;
            }
            g_layoutManager->changeFloatingMode(w->m_target);
        } else if (!st.floatingManaged) {
            // Was already floating before we ever touched it — leave it so on restore.
            st.floatingBefore  = true;
            st.floatingManaged = true;
        }

        if (i == 0) {
            // Focused card: apply the default centered box only once per stint,
            // then hand the box to the user (resizable/draggable) — layoutStack
            // never re-pins it while this window stays focused.
            if (!st.frontBoxSet) {
                w->m_target->setPositionGlobal(frontBox);
                st.frontBoxSet = true;
            }
            // Live geometry of the focused card anchors every back card.
            frontBox = w->m_target->position();
            continue;
        }

        CBox target;
        {
            const double s  = getTransformForLayer((int)i).scale;
            const double cw = frontBox.w * s;
            const double ch = frontBox.h * s;

            // Scatter each back card around the front card at a random angle and
            // radius — both hashed from the window address, so positions look
            // random but never jitter across focus changes. The offset pushes the
            // card's center past the front card's half-size by PEEK_MIN..PEEK_MAX,
            // so every card protrudes past the opaque front footprint and stays
            // visible.
            const uint64_t seed   = randSeed(addr);
            const double   angle  = (double)((seed >> 0) & 0xFFFF) / 65536.0 * 2.0 * M_PI;
            const double   radius = PEEK_MIN + (double)((seed >> 32) & 0xFFFF) / 65536.0 * (PEEK_MAX - PEEK_MIN);
            const double   fcx    = frontBox.x + frontBox.w / 2.0;
            const double   fcy    = frontBox.y + frontBox.h / 2.0;
            const double   ox     = std::cos(angle) * (frontBox.w / 2.0 + radius);
            const double   oy     = std::sin(angle) * (frontBox.h / 2.0 + radius);

            target = CBox{fcx + ox - cw / 2.0, fcy + oy - ch / 2.0, cw, ch};

            // Safety net: keep every card on-screen.
            CBox work = anchor->logicalBoxMinusReserved();
            if (target.x < work.x)
                target.x = work.x;
            if (target.y < work.y)
                target.y = work.y;
            if (target.x + target.w > work.x + work.w)
                target.x = work.x + work.w - target.w;
            if (target.y + target.h > work.y + work.h)
                target.y = work.y + work.h - target.h;
        }
        w->m_target->setPositionGlobal(target);
    }

    // Render order must match the stack. renderWorkspaceWindows walks
    // windowState()->windows() in order (later = on top), and focus changes via
    // cyclenext only call bringTargetToTop (a no-op outside groups) — so raise
    // the stack from deepest to focused to put the focused window on top.
    for (size_t i = m_zStack.size(); i > 0; i--) {
        auto w = m_zStack[i - 1].lock();
        if (valid(w))
            Desktop::windowState()->raise(w);
    }

    DebugLog::log("layoutStack count=" + std::to_string(m_zStack.size()));

    // Deck is live — pull the wallpaper back into the depth scene.
    pullWallpaper(true);
}

void CDepthFocusManager::restoreWindow(PHLWINDOW pWindow) {
    if (!valid(pWindow))
        return;

    // Drop the depth shadow decoration if present.
    for (const auto& deco : pWindow->m_windowDecorations) {
        auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
        if (shadowDeco) {
            HyprlandAPI::removeWindowDecoration(PHANDLE, shadowDeco);
            break;
        }
    }

    // Reset alpha to full. Geometry is untouched: scaling was render-only.
    *pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE) = 1.0f;

    // Drop any scale transformer: the window leaves the transformed path.
    setWindowScale(pWindow, 1.0f);

    // Return a window we floated back to tiling (if it wasn't floating before).
    if (pWindow->m_isMapped) {
        const auto it = m_applied.find((uintptr_t)pWindow.get());
        if (it != m_applied.end() && it->second.floatingManaged && !it->second.floatingBefore && pWindow->m_isFloating)
            g_layoutManager->changeFloatingMode(pWindow->m_target);
    }

    g_pHyprRenderer->damageWindow(pWindow, true);

    DebugLog::log("restoreWindow win=" + winStr(pWindow));
}

void CDepthFocusManager::refreshDepthCache() {
    const int maxLayers = std::max<int>(1, g_iMaxLayers ? g_iMaxLayers->value() : 8);
    m_depthCache.clear();
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (valid(w))
            m_depthCache[(uintptr_t)w.get()] = std::min<int>((int)i, maxLayers - 1);
    }
}

void CDepthFocusManager::onRenderStage(eRenderStage stage) {
    if (g_bEnabled && !g_bEnabled->value())
        return;

    // Throttled render-loop detector: if the main thread busy-loops re-rendering,
    // these lines keep appearing while handler logs and heartbeat cpu_delta tell
    // the rest of the story.
    if (DebugLog::isEnabled()) {
        static int stageCount = 0;
        if (++stageCount % 250 == 0) {
            auto& rd   = g_pHyprRenderer->m_renderData;
            auto  pWin = rd.currentWindow.lock();
            DebugLog::log("render stage=" + std::to_string((int)stage) + " n=" + std::to_string(stageCount) +
                          " win=" + (pWin ? winStr(pWin) : std::string("(null)")));
        }
    }

    // NOTE: per-window visual scaling via m_renderData.renderModif is NOT possible
    // on Hyprland 0.56.1. renderModif is one global SRenderModifData, applied to
    // every pass element at draw time — CRenderPass::render executes after all
    // RENDER_* hooks of the walk (Renderer.cpp:187-195) — and it is never reset:
    // beginRender/endRender don't touch it, and CRendererHintsPassElement (the
    // only reset path, ElementRenderer.cpp:190-194) is only pushed for workspace
    // translate/scale animations. A SCALECENTER push here leaked to the focused
    // window, to wallpaper/layers/cursor, and across frames, corrupting damage
    // tracking and pegging the main thread in a render storm that froze the
    // session. Depth is conveyed by opacity + CDepthShadowDecoration +
    // CScaleTransformer (the official per-window fb pipeline), all applied in
    // applyDepthToWindow — no render-hook mutations.
}
