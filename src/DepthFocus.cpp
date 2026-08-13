#include "DepthFocus.hpp"
#include "DepthShadow.hpp"
#include "DebugLog.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace focusz::constants {
    constexpr int    kMaxWindows      = 32;
    constexpr double kWorkareaMargin  = 48.0;
    constexpr double kEdgeInset       = 16.0;
    constexpr float  kFloorScale      = 0.22f;
    constexpr float  kFloorOpacity    = 0.05f;
} // namespace focusz::constants

static std::string winStr(PHLWINDOW pWindow) {
    return pWindow ? "0x" + std::to_string((uintptr_t)pWindow.get()) : "(null)";
}

// Attach/update/remove the scale transformer for a window.
//
// DISABLED: the transformer's transform() runs inside Hyprland's renderWindow
// path and SEGVs on Hyprland v0.56.2 (see hyprlandCrashReport*.txt — the
// backtrace lands in libfocusZ.so during renderWindow). We never attach it
// now; the depth scale is conveyed purely by layoutStack()'s per-card geometry
// (back cards get smaller boxes) plus per-window opacity, both safe. This keeps
// the plugin render-path-free so it can no longer crash the compositor.
static void setWindowScale(PHLWINDOW pWindow, float /*scale*/) {
    if (!valid(pWindow))
        return;
}

// Sync the debug logger with the plugin:focusZ:debug config value (runtime toggle).
static void syncDebug() {
    DebugLog::setEnabled(cfg().debug->value());
}

CDepthFocusManager::CDepthFocusManager() {
    // Do NOT call init() here — config values are not ready during PLUGIN_INIT.
    // init() will be called on the first event (onWindowOpen or onFocusChange).
    m_needsInit = true;
}

CDepthFocusManager::~CDepthFocusManager() {
    // Restore every tracked window before the plugin tears down: remove the
    // depth shadow, reset alpha to 1.0, drop the scale transformer and unfloat
    // windows we floated. If this is skipped, windows keep a transformer and
    // decoration whose code lives in this .so — the next frame dereferences
    // unmapped memory and the compositor SEGVs (a real crash on plugin unload).
    std::vector<PHLWINDOW> wins;
    for (auto& [addr, st] : m_applied) {
        if (auto w = st.window.lock())
            wins.push_back(w);
    }
    for (auto& w : wins)
        restoreWindow(w);
}

SLayerTransform CDepthFocusManager::getTransformForLayer(int depth) const {
    SLayerTransform t;
    if (depth <= 0) {
        // Layer 0: focused window, full scale
        t.scale   = 1.0f;
        t.opacity = 1.0f;
    } else if (depth == 1) {
        t.scale   = cfg().layer1Scale->value();
        t.opacity = cfg().layer1Opacity->value();
    } else if (depth == 2) {
        t.scale   = cfg().layer2Scale->value();
        t.opacity = cfg().layer2Opacity->value();
    } else {
        // Deeper layers: interpolate from layer 2 down to the floor across the
        // remaining configured depth levels. A fixed per-layer step collapsed to
        // the floor after only ~2 extra layers, so every window past depth 3
        // looked identical — the "blur/dim only happens once" bug. Spreading the
        // falloff over max_layers keeps each deeper card visibly dimmer/smaller.
        const float baseScale   = cfg().layer2Scale->value();
        const float baseOpacity = cfg().layer2Opacity->value();
        const float floorScale  = focusz::constants::kFloorScale;
        const float floorOpacity = focusz::constants::kFloorOpacity;
        const int   maxLayers   = std::max<int>(1, cfg().maxLayers->value());
        const int   extraDepth  = depth - 2;
        const int   totalSteps  = std::max(1, maxLayers - 2); // levels past layer 2
        const float frac        = std::min(1.0f, (float)extraDepth / (float)totalSteps);
        t.scale   = baseScale + (floorScale - baseScale) * frac;
        t.opacity = baseOpacity + (floorOpacity - baseOpacity) * frac;
    }
    return t;
}

float CDepthFocusManager::getDeckFactor() const {
    // Deck "fullness": 0 with just the focused window, 1 once the stack reaches
    // max_layers. Each window past the first adds an equal share.
    const size_t n       = m_zStack.size();
    const int    maxL    = std::max<int>(1, cfg().maxLayers->value());
    const size_t denom   = std::max<size_t>(1, (size_t)maxL - 1);
    return std::clamp((float)(n - 1) / (float)denom, 0.0f, 1.0f);
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
    if (!cfg().enabled->value()) {
        for (const auto& ref : m_zStack) {
            auto w = ref.lock();
            if (valid(w))
                restoreWindow(w);
        }
        m_zStack.clear();
        m_applied.clear();
        m_depthCache.clear();
        m_monitor = {};
        m_workspace = {};
        return;
    }

    const auto& windows = Desktop::windowState()->windows();
    const auto focused  = Desktop::focusState()->window();

    // Anchor the stack to the focused window's monitor: background layers stay
    // isolated per workspace/monitor, so windows on other monitors are untouched.
    if (valid(focused))
        m_monitor = focused->m_monitor;
    const auto anchor = m_monitor.lock();
    if (anchor)
        m_workspace = anchor->m_activeWorkspace;

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
    if ((int)next.size() > focusz::constants::kMaxWindows)
        next.resize(focusz::constants::kMaxWindows);

    m_zStack.clear();
    for (auto& w : next)
        m_zStack.emplace_back(w);

    // New deal: back-card spawn positions reshuffle so the scatter reads freshly
    // random (config card_scatter_reshuffle gates whether layoutStack uses it).
    m_dealNonce++;

    DebugLog::log("rebuildStack stack_size=" + std::to_string(m_zStack.size()) + " monitor=" +
                  (m_monitor.lock() ? "set" : "none"));

    applyAllDepthTransforms();
    DebugLog::log("rebuildStack exit");
}

void CDepthFocusManager::onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason /*reason*/) {
    // Deferred init: config values are ready now (first event after PLUGIN_INIT).
    if (m_needsInit) {
        m_needsInit = false;
        init();
    }

    syncDebug();
    if (!cfg().enabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    DebugLog::log("onFocusChange win=" + winStr(pWindow));

    // Re-anchor whenever the focused window is on a different monitor OR a
    // different workspace of the same monitor. Previously only a monitor change
    // rebuilt the stack, so focusing a window on another workspace promoted it
    // onto the old stack — the deck mixed windows from two workspaces and the
    // dim/blur only updated when clicking the window you wanted to see change.
    const auto anchor = m_monitor.lock();
    if (!anchor || !(pWindow->m_monitor.lock() == anchor) ||
        (pWindow->m_workspace && pWindow->m_workspace != m_workspace.lock())) {
        rebuildStack();
        DebugLog::log("onFocusChange exit (rebuild)");
        return;
    }

    promoteWindow(pWindow);
    applyAllDepthTransforms();
    DebugLog::log("onFocusChange exit");
}

void CDepthFocusManager::onWindowOpen(PHLWINDOW pWindow) {
    // Deferred init: config values are ready now (first event after PLUGIN_INIT).
    if (m_needsInit) {
        m_needsInit = false;
        init();
    }

    syncDebug();
    if (!cfg().enabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    DebugLog::log("onWindowOpen win=" + winStr(pWindow));

    // Only windows on the anchor monitor join this stack.
    if (m_monitor.lock() && !(pWindow->m_monitor.lock() == m_monitor.lock()))
        return;

    // A window opened on another workspace of the same monitor: re-anchor the
    // deck to that workspace (matches onFocusChange, prevents mixing).
    if (pWindow->m_workspace && pWindow->m_workspace != m_workspace.lock()) {
        rebuildStack();
        DebugLog::log("onWindowOpen exit (rebuild)");
        return;
    }

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
    if (!cfg().enabled->value())
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

    if (m_zStack.empty()) {
        m_monitor    = {};
        m_workspace  = {};
    }

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
    if ((int)m_zStack.size() > focusz::constants::kMaxWindows)
        m_zStack.resize(focusz::constants::kMaxWindows);

    DebugLog::log("promoteWindow win=" + winStr(pWindow) + " stack_size=" + std::to_string(m_zStack.size()));
}

void CDepthFocusManager::applyAllDepthTransforms() {
    // stacking toggled since last apply: switch modes (float+scale boxes ↔
    // tiled+content-scale), not just re-apply the same windows.
    const bool stackingNow = cfg().stacking->value();
    if (stackingNow != m_stackingActive) {
        const bool turningOff = m_stackingActive && !stackingNow;
        m_stackingActive      = stackingNow;
        if (turningOff) {
            for (const auto& ref : m_zStack) {
                auto w = ref.lock();
                if (valid(w))
                    restoreWindow(w);
            }
            m_zStack.clear();
            m_applied.clear();
            m_depthCache.clear();
            m_monitor    = {};
            m_workspace  = {};
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
    const int    maxLayers = std::max<int>(1, cfg().maxLayers->value());
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

    // Propagate the deck factor to every depth shadow: it changes when windows
    // open/close even for cards whose layer depth stays the same.
    const float deckFactor = getDeckFactor();
    for (auto& [addr, st] : m_applied) {
        auto w = st.window.lock();
        if (!valid(w) || !st.decorated)
            continue;
        for (const auto& deco : w->m_windowDecorations) {
            auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
            if (shadowDeco) {
                shadowDeco->setDeckFactor(deckFactor);
                break;
            }
        }
    }

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

    const bool stacking = cfg().stacking->value();
    setWindowScale(pWindow, stacking ? 1.0f : transform.scale);

    // Depth shadow decoration is a no-op placeholder (its draw() is empty) and
    // still runs on Hyprland's renderWindow path; we don't attach it anymore so
    // the plugin stays fully render-path-free and cannot crash the compositor.
    state.decorated = true;

    g_pHyprRenderer->damageWindow(pWindow, true);
}

// Deterministic splitmix64 finalizer from a uintptr_t seed.
static uint64_t randSeed(uintptr_t seed) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return h;
}

// Ensure a window is floating so we can own its box via setPositionGlobal.
void CDepthFocusManager::ensureFloating(PHLWINDOW w, SAppliedState& st) {
    if (!w->m_isFloating) {
        if (!st.floatingManaged) {
            st.floatingBefore  = false;
            st.floatingManaged = true;
        }
        g_layoutManager->changeFloatingMode(w->m_target);
    } else if (!st.floatingManaged) {
        st.floatingBefore  = true;
        st.floatingManaged = true;
    }
}

// Scatter a back card near a random workarea edge (edge-bias mode).
// Re-rolls up to 64 times to guarantee at least PEEK_MIN px peek.
CBox CDepthFocusManager::scatterOnEdges(
    Monitor::CMonitor* anchor, const CBox& frontBox,
    double cw, double ch, uintptr_t addr, size_t depth,
    uint64_t nonceMix, double peekMin) {

    CBox   work     = anchor->logicalBoxMinusReserved();
    CBox   best     = CBox{work.x, work.y, cw, ch};
    double bestPeek = -1.0;
    constexpr double EDGE_INSET = focusz::constants::kEdgeInset;

    for (int attempt = 0; attempt < 64; attempt++) {
        const uint64_t r = randSeed(addr ^ ((uint64_t)depth << 32) ^ nonceMix ^
                                    ((uint64_t)attempt * 0x9E3779B97F4A7C15ull));
        const int    edge  = (int)(r >> 0) & 0x3;
        const double along = (double)((r >> 16) & 0xFFFF) / 65536.0;
        double tx, ty;
        switch (edge) {
            case 0: // top
                tx = work.x + along * std::max(0.0, work.w - cw);
                ty = work.y + EDGE_INSET;
                break;
            case 1: // right
                tx = work.x + work.w - cw - EDGE_INSET;
                ty = work.y + along * std::max(0.0, work.h - ch);
                break;
            case 2: // bottom
                tx = work.x + along * std::max(0.0, work.w - cw);
                ty = work.y + work.h - ch - EDGE_INSET;
                break;
            default: // left
                tx = work.x + EDGE_INSET;
                ty = work.y + along * std::max(0.0, work.h - ch);
                break;
        }
        const double exL    = frontBox.x - tx;
        const double exR    = tx + cw - (frontBox.x + frontBox.w);
        const double exT    = frontBox.y - ty;
        const double exB    = ty + ch - (frontBox.y + frontBox.h);
        const double peekAmt = std::max(std::max(exL, exR), std::max(exT, exB));
        if (peekAmt > bestPeek) {
            bestPeek = peekAmt;
            best     = CBox{tx, ty, cw, ch};
        }
        if (peekAmt >= peekMin)
            break;
    }
    return best;
}

// Scatter a back card around the focused card at a random angle/radius.
CBox CDepthFocusManager::scatterAroundFocused(
    const CBox& frontBox, double cw, double ch,
    uint64_t seed, double peekMin, double peekMax) {

    const double angle  = (double)((seed >> 0) & 0xFFFF) / 65536.0 * 2.0 * M_PI;
    const double radius = peekMin + (double)((seed >> 48) & 0xFFFF) / 65536.0 * (peekMax - peekMin);
    const double fcx    = frontBox.x + frontBox.w / 2.0;
    const double fcy    = frontBox.y + frontBox.h / 2.0;
    const double ox     = std::cos(angle) * (frontBox.w / 2.0 + radius);
    const double oy     = std::sin(angle) * (frontBox.h / 2.0 + radius);
    return CBox{fcx + ox - cw / 2.0, fcy + oy - ch / 2.0, cw, ch};
}

// Clamp a card box to stay on-screen.
CBox CDepthFocusManager::clampToWorkarea(const CBox& target, Monitor::CMonitor* anchor) {
    CBox work          = anchor->logicalBoxMinusReserved();
    CBox clamped       = target;
    if (clamped.x < work.x)
        clamped.x = work.x;
    if (clamped.y < work.y)
        clamped.y = work.y;
    if (clamped.x + clamped.w > work.x + work.w)
        clamped.x = work.x + work.w - clamped.w;
    if (clamped.y + clamped.h > work.y + work.h)
        clamped.y = work.y + work.h - clamped.h;
    return clamped;
}

void CDepthFocusManager::layoutStack() {
    if (!cfg().stacking->value())
        return;
    if (m_zStack.empty())
        return;

    auto focused = m_zStack[0].lock();
    if (!valid(focused))
        return;
    auto anchor = focused->m_monitor.lock();
    if (!anchor)
        return;

    const double FRONT_SCALE = cfg().frontScale->value();
    const bool   EDGE        = cfg().edgeScatter->value();
    const double PEEK_MIN    = cfg().peekMin->value();
    const double PEEK_MAX    = cfg().peekMax->value();

    CBox base = anchor->logicalBoxMinusReserved();
    base      = CBox{base.x + focusz::constants::kWorkareaMargin,
                     base.y + focusz::constants::kWorkareaMargin,
                     base.w - 2 * focusz::constants::kWorkareaMargin,
                     base.h - 2 * focusz::constants::kWorkareaMargin};

    CBox frontBox = CBox{base.x + base.w * (1.0 - FRONT_SCALE) / 2.0,
                         base.y + base.h * (1.0 - FRONT_SCALE) / 2.0,
                         base.w * FRONT_SCALE,
                         base.h * FRONT_SCALE};

    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;
        if (Fullscreen::controller()->isFullscreen(w))
            continue;

        const uintptr_t addr = (uintptr_t)w.get();
        auto&           st   = m_applied[addr];

        ensureFloating(w, st);

        if (i == 0) {
            if (!st.frontBoxSet) {
                w->m_target->setPositionGlobal(frontBox);
                st.frontBoxSet = true;
            }
            frontBox = w->m_target->position();
            continue;
        }

        const double s  = getTransformForLayer((int)i).scale;
        const double cw = frontBox.w * s;
        const double ch = frontBox.h * s;

        const uint64_t nonceMix =
            (cfg().scatterReshuffle->value()) ? m_dealNonce * 0x9E3779B97F4A7C15ull : 0ull;
        const uint64_t seed = randSeed(addr ^ ((uint64_t)i << 32) ^ nonceMix);

        CBox target;
        if (EDGE) {
            target = scatterOnEdges(anchor.get(), frontBox, cw, ch, addr, i, nonceMix, PEEK_MIN);
        } else {
            target = scatterAroundFocused(frontBox, cw, ch, seed, PEEK_MIN, PEEK_MAX);
        }

        w->m_target->setPositionGlobal(clampToWorkarea(target, anchor.get()));
    }

    for (size_t i = m_zStack.size(); i > 0; i--) {
        auto w = m_zStack[i - 1].lock();
        if (valid(w))
            Desktop::windowState()->raise(w);
    }

    DebugLog::log("layoutStack count=" + std::to_string(m_zStack.size()));
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
    const int maxLayers = std::max<int>(1, cfg().maxLayers->value());
    m_depthCache.clear();
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (valid(w))
            m_depthCache[(uintptr_t)w.get()] = std::min<int>((int)i, maxLayers - 1);
    }
}

void CDepthFocusManager::onRenderStage(eRenderStage /*stage*/) {
    // Render hook disabled: drawCardBorders() called g_pHyprRenderer->addPassElement()
    // during RENDER_POST_WINDOWS, which SEGVs inside Hyprland's addPassElement on
    // v0.56.2 (see hyprlandCrashReport*.txt). The deck is done with safe
    // layout + opacity only, so nothing needs to run here.
}
