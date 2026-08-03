#include "DepthFocus.hpp"
#include "DepthShadow.hpp"
#include "DebugLog.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <cmath>

static std::string winStr(PHLWINDOW pWindow) {
    return pWindow ? "0x" + std::to_string((uintptr_t)pWindow.get()) : "(null)";
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
        // Deeper layers: extrapolate from layer 2
        const float baseScale   = g_fLayer2Scale  ? g_fLayer2Scale->value()  : 0.70f;
        const float baseOpacity = g_fLayer2Opacity ? g_fLayer2Opacity->value() : 0.4f;
        const int   extraDepth  = depth - 2;
        t.scale   = std::max(0.3f, baseScale - 0.1f * extraDepth);
        t.opacity = std::max(0.1f, baseOpacity - 0.15f * extraDepth);
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
        next.push_back(w);
    }

    // Cap to max_layers; evicted windows are restored by applyAllDepthTransforms.
    const int maxLayers = g_iMaxLayers ? g_iMaxLayers->value() : 3;
    if ((int)next.size() > maxLayers)
        next.resize(maxLayers);

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

    // Insert at front (Layer 0)
    m_zStack.insert(m_zStack.begin(), pWindow);

    // Cap to max_layers; the tail is restored by applyAllDepthTransforms.
    const int maxLayers = g_iMaxLayers ? g_iMaxLayers->value() : 3;
    if ((int)m_zStack.size() > maxLayers)
        m_zStack.resize(maxLayers);

    DebugLog::log("promoteWindow win=" + winStr(pWindow) + " stack_size=" + std::to_string(m_zStack.size()));
}

void CDepthFocusManager::applyAllDepthTransforms() {
    refreshDepthCache();

    // Restore windows that left the stack (evicted by max_layers, moved monitors, closed).
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
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;

        const uintptr_t addr = (uintptr_t)w.get();
        auto            it   = m_applied.find(addr);
        if (it != m_applied.end() && it->second.depth == (int)i)
            continue;

        applyDepthToWindow(w, (int)i);
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

    DebugLog::log("applyDepthToWindow win=" + winStr(pWindow) + " depth=" + std::to_string(depth) +
                  " opacity=" + std::to_string(transform.opacity) + " (scale inert)");

    // Opacity is a pure render property — safe on tiled and floating windows alike.
    auto& alphaVar = pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE);
    *alphaVar = transform.opacity;

    // NOTE: geometry is intentionally NOT touched here, and renderModif is NOT
    // used either. Physically resizing windows (sizeAnim/posAnim) fought
    // Hyprland's layout engine, which re-arranges tiled windows back every frame
    // — that oscillation pegged the main thread and froze the session. Pushing
    // RMOD_TYPE_SCALECENTER into m_renderData.renderModif was also a freeze: the
    // global modif is applied to every pass element at draw time and never reset
    // (see onRenderStage), so the scale leaked to the focused window and across
    // frames. Depth is conveyed purely by opacity + shadow below.

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
    g_pHyprRenderer->damageWindow(pWindow, true);

    DebugLog::log("restoreWindow win=" + winStr(pWindow));
}

void CDepthFocusManager::refreshDepthCache() {
    m_depthCache.clear();
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (valid(w))
            m_depthCache[(uintptr_t)w.get()] = (int)i;
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
    // session. Depth is conveyed purely by opacity + CDepthShadowDecoration,
    // both applied in applyDepthToWindow — no render-hook mutations.
}
