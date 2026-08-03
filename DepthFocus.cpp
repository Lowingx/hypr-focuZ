#include "DepthFocus.hpp"
#include "DepthShadow.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <cmath>

// Helper: compute the gap-to-center offset for a window being scaled down.
// When a window at (wx, wy) with size (ww, wh) is scaled by `scale`,
// we want it to appear centered at the same position.
static Vector2D scaleOffsetForCenter(const Vector2D& wpos, const Vector2D& wsize, float scale) {
    if (!g_bCenterScale || !g_bCenterScale->value())
        return {};

    // Desired visual size after scale
    const float newW = wsize.x * scale;
    const float newH = wsize.y * scale;

    // Center of original window
    const float cx = wpos.x + wsize.x / 2.0f;
    const float cy = wpos.y + wsize.y / 2.0f;

    // New top-left so that the center stays the same
    const float newX = cx - newW / 2.0f;
    const float newY = cy - newH / 2.0f;

    return {newX - wpos.x, newY - wpos.y};
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

    applyAllDepthTransforms();
}

void CDepthFocusManager::onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason /*reason*/) {
    if (g_bEnabled && !g_bEnabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    // Cross-monitor (or first) focus: re-anchor the whole stack.
    if (!m_monitor.lock() || !(pWindow->m_monitor.lock() == m_monitor.lock())) {
        rebuildStack();
        return;
    }

    promoteWindow(pWindow);
    applyAllDepthTransforms();
}

void CDepthFocusManager::onWindowOpen(PHLWINDOW pWindow) {
    if (g_bEnabled && !g_bEnabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    // Only windows on the anchor monitor join this stack.
    if (m_monitor.lock() && !(pWindow->m_monitor.lock() == m_monitor.lock()))
        return;

    promoteWindow(pWindow);
    applyAllDepthTransforms();
}

void CDepthFocusManager::onWindowClose(PHLWINDOW pWindow) {
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

    // The window is gone; drop tracking so restore never touches a dead window.
    m_applied.erase((uintptr_t)pWindow.get());

    if (m_zStack.empty())
        m_monitor = {};

    applyAllDepthTransforms();
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
}

void CDepthFocusManager::applyDepthToWindow(PHLWINDOW pWindow, int depth) {
    if (!valid(pWindow))
        return;

    const uintptr_t addr = (uintptr_t)pWindow.get();
    const auto      transform = getTransformForLayer(depth);

    auto& state = m_applied[addr];
    state.window = pWindow;
    state.depth  = depth;

    auto& alphaVar = pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE);
    *alphaVar = transform.opacity;

    auto& sizeAnim = pWindow->sizeAnimation();
    auto& posAnim  = pWindow->positionAnimation();

    if (depth <= 0) {
        // Focused/restored: full alpha plus original geometry. Restoring size/pos here
        // is what unwinds windows (esp. floating, which layout never re-arranges) that
        // were scaled while in the background. orig is never re-captured at depth 0 so
        // rapid focus cycles can't compound the scale.
        if (!state.origValid) {
            state.origSize  = sizeAnim->goal();
            state.origPos   = posAnim->goal();
            state.origValid = true;
        }
        *sizeAnim = state.origSize;
        *posAnim  = state.origPos;
    } else {
        // Capture the unscaled layout geometry on first touch; re-capture if the
        // layout resized us up (e.g. another window closed) so scale never compounds.
        if (!state.origValid ||
            sizeAnim->goal().x > state.origSize.x * transform.scale + 0.5f ||
            sizeAnim->goal().y > state.origSize.y * transform.scale + 0.5f) {
            state.origSize  = sizeAnim->goal();
            state.origPos   = posAnim->goal();
            state.origValid = true;
        }

        *sizeAnim = state.origSize * transform.scale;
        if (g_bCenterScale && g_bCenterScale->value()) {
            const auto offset = scaleOffsetForCenter(state.origPos, state.origSize, transform.scale);
            *posAnim = state.origPos + offset;
        }
    }

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

    // Reset to the original unscaled geometry.
    const uintptr_t addr = (uintptr_t)pWindow.get();
    auto            it   = m_applied.find(addr);
    if (it != m_applied.end() && it->second.origValid) {
        *pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE) = 1.0f;
        *pWindow->sizeAnimation()  = it->second.origSize;
        *pWindow->positionAnimation() = it->second.origPos;
        g_pHyprRenderer->damageWindow(pWindow, true);
    }
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

    if (stage == RENDER_PRE_WINDOW) {
        auto& rd    = g_pHyprRenderer->m_renderData;
        auto  pWin  = rd.currentWindow.lock();
        if (!valid(pWin))
            return;

        // O(1) depth lookup via cache (per-frame hot path).
        const int depth = getLayerDepth(pWin);
        if (depth <= 0)
            return; // Focused window: no modification

        const auto transform = getTransformForLayer(depth);
        if (transform.scale >= 0.99f)
            return;

        // Apply center-based scale modification to render data
        rd.renderModif.modifs.push_back(std::make_pair(
            Render::SRenderModifData::RMOD_TYPE_SCALECENTER,
            std::any(transform.scale)));
    }
}
