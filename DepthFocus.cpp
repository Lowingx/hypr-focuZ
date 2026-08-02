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
        t.blur    = false;
    } else if (depth == 1) {
        t.scale   = g_fLayer1Scale  ? g_fLayer1Scale->value()  : 0.85f;
        t.opacity = g_fLayer1Opacity ? g_fLayer1Opacity->value() : 0.7f;
        t.blur    = g_bLayer1Blur    ? g_bLayer1Blur->value()    : true;
    } else if (depth == 2) {
        t.scale   = g_fLayer2Scale  ? g_fLayer2Scale->value()  : 0.70f;
        t.opacity = g_fLayer2Opacity ? g_fLayer2Opacity->value() : 0.4f;
        t.blur    = g_bLayer2Blur    ? g_bLayer2Blur->value()    : true;
    } else {
        // Deeper layers: extrapolate from layer 2
        const float baseScale   = g_fLayer2Scale  ? g_fLayer2Scale->value()  : 0.70f;
        const float baseOpacity = g_fLayer2Opacity ? g_fLayer2Opacity->value() : 0.4f;
        const int   extraDepth  = depth - 2;
        t.scale   = std::max(0.3f, baseScale - 0.1f * extraDepth);
        t.opacity = std::max(0.1f, baseOpacity - 0.15f * extraDepth);
        t.blur    = true;
    }
    return t;
}

int CDepthFocusManager::getLayerDepth(PHLWINDOW pWindow) const {
    if (!valid(pWindow))
        return -1;

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
    m_zStack.clear();

    if (g_bEnabled && !g_bEnabled->value())
        return;

    const auto& windows = Desktop::windowState()->windows();
    const auto focused  = Desktop::focusState()->window();

    // Put focused window first
    if (valid(focused))
        m_zStack.push_back(focused);

    // Add remaining windows in their current Z order (excluding focused)
    for (const auto& w : windows) {
        if (!w->m_isMapped || w->isHidden())
            continue;
        if (w == focused)
            continue;
        m_zStack.push_back(w);
    }

    // Cap to max_layers
    const int maxLayers = g_iMaxLayers ? g_iMaxLayers->value() : 3;
    if ((int)m_zStack.size() > maxLayers)
        m_zStack.resize(maxLayers);

    applyAllDepthTransforms();
}

void CDepthFocusManager::onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason /*reason*/) {
    if (g_bEnabled && !g_bEnabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    promoteWindow(pWindow);
    applyAllDepthTransforms();
}

void CDepthFocusManager::onWindowOpen(PHLWINDOW pWindow) {
    if (g_bEnabled && !g_bEnabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
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

    if (found)
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

    // Cap to max_layers
    const int maxLayers = g_iMaxLayers ? g_iMaxLayers->value() : 3;
    if ((int)m_zStack.size() > maxLayers)
        m_zStack.resize(maxLayers);
}

void CDepthFocusManager::applyAllDepthTransforms() {
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (valid(w))
            applyDepthToWindow(w, (int)i);
    }
}

void CDepthFocusManager::applyDepthToWindow(PHLWINDOW pWindow, int depth) {
    if (!valid(pWindow))
        return;

    const auto transform = getTransformForLayer(depth);

    // Apply opacity via WINDOW_ALPHA_ACTIVE animated variable.
    // Setting the value triggers Hyprland's built-in animation system.
    auto& alphaVar = pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE);
    *alphaVar = transform.opacity;

    // Apply size animation for scaling effect
    auto& sizeAnim       = pWindow->sizeAnimation();
    const auto goalSize  = sizeAnim->goal();
    *sizeAnim = goalSize * transform.scale;

    // Apply position animation for center-based offset
    if (g_bCenterScale && g_bCenterScale->value() && transform.scale < 1.0f) {
        auto& posAnim      = pWindow->positionAnimation();
        const auto goalPos = posAnim->goal();
        const auto offset  = scaleOffsetForCenter(goalPos, goalSize / transform.scale, transform.scale);
        *posAnim = goalPos + offset;
    }

    // Add depth shadow decoration if not already present (only for background windows)
    if (depth > 0) {
        const uintptr_t addr = (uintptr_t)pWindow.get();
        if (!m_decoratedWindows.contains(addr)) {
            HyprlandAPI::addWindowDecoration(PHANDLE, pWindow, Hyprutils::Memory::makeUnique<CDepthShadowDecoration>(pWindow, depth));
            m_decoratedWindows[addr] = true;
        } else {
            // Update depth on existing decoration
            for (const auto& deco : pWindow->m_windowDecorations) {
                auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
                if (shadowDeco)
                    shadowDeco->setDepth(depth);
            }
        }
    }

    // Damage the window to ensure re-render
    g_pHyprRenderer->damageWindow(pWindow, true);
}

void CDepthFocusManager::onRenderStage(eRenderStage stage) {
    if (g_bEnabled && !g_bEnabled->value())
        return;

    if (stage == RENDER_PRE_WINDOW) {
        auto& rd    = g_pHyprRenderer->m_renderData;
        auto  pWin  = rd.currentWindow.lock();
        if (!valid(pWin))
            return;

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
