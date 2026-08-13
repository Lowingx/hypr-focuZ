#include "DepthShadow.hpp"
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <cmath>

CDepthShadowDecoration::CDepthShadowDecoration(PHLWINDOW window, int depth)
    : IHyprWindowDecoration(window), m_window(window), m_depth(depth) {}

SDecorationPositioningInfo CDepthShadowDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_ABSOLUTE;
    info.desiredExtents = {};
    return info;
}

void CDepthShadowDecoration::onPositioningReply(const SDecorationPositioningReply& /*reply*/) {
    // Nothing needed: we render using absolute positioning
}

void CDepthShadowDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    if (!cfg().enabled->value())
        return;

    auto w = m_window.lock();
    if (!valid(w) || !w->m_isMapped)
        return;

    // Hyprland calls draw() only for the correct monitor — the old
    // w->m_monitor.lock() comparison crashed because the WP is corrupted
    // during the first render of a newly-mapped window.  Trust pMonitor.
    drawShadow(pMonitor, a);
}

void CDepthShadowDecoration::drawShadow(PHLMONITOR /*pMonitor*/, float const& /*a*/) {
    // DISABLED — g_pHyprRenderer->drawShadow() touches m_renderData.pMonitor
    // internally, which SEGVs when the monitor is re-leased/modeset while a
    // window opens. Ryoku's native decoration.shadow (range 45) already draws
    // window shadows, so this decoration is a no-op placeholder that keeps the
    // plugin API surface (deco type, damage) intact without touching hardware.
    return;
}

eDecorationType CDepthShadowDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CDepthShadowDecoration::updateWindow(PHLWINDOW pWindow) {
    m_window = pWindow;
}

void CDepthShadowDecoration::damageEntire() {
    auto w = m_window.lock();
    if (valid(w))
        g_pHyprRenderer->damageWindow(w, true);
}
