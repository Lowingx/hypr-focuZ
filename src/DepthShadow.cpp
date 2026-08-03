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
    if (!g_bEnabled->value())
        return;

    auto w = m_window.lock();
    if (!valid(w) || !w->m_isMapped)
        return;

    // Only draw if the window is on this monitor
    if (w->m_monitor.lock() != pMonitor)
        return;

    drawShadow(pMonitor, a);
}

void CDepthShadowDecoration::drawShadow(PHLMONITOR /*pMonitor*/, float const& a) {
    auto w = m_window.lock();
    if (!valid(w))
        return;

    const auto box = w->getWindowMainSurfaceBox();
    if (box.w <= 0 || box.h <= 0)
        return;

    // Shadow parameters based on depth
    // Layer 0: max shadow, Layer 1: medium, Layer 2+: minimal
    int   shadowRange = 0;
    int   shadowOffsetX = 0;
    int   shadowOffsetY = 0;

    switch (m_depth) {
        case 1:
            shadowRange    = 20;
            shadowOffsetX  = 8;
            shadowOffsetY  = 6;
            break;
        case 2:
            shadowRange    = 12;
            shadowOffsetX  = 4;
            shadowOffsetY  = 3;
            break;
        default:
            shadowRange    = std::max(4, 12 - (m_depth - 2) * 3);
            shadowOffsetX  = std::max(1, 4 - (m_depth - 2));
            shadowOffsetY  = std::max(1, 3 - (m_depth - 2));
            break;
    }

    const float shadowAlpha = a * (m_depth == 1 ? 0.5f : m_depth == 2 ? 0.3f : 0.15f);

    CBox shadowBox = box;
    shadowBox.x += shadowOffsetX;
    shadowBox.y += shadowOffsetY;
    shadowBox.w = box.w;
    shadowBox.h = box.h;

    const int rounding = w->rounding();

    // Create a solid dark shadow color
    Config::CGradientValueData shadowColor(CHyprColor(0, 0, 0, shadowAlpha));

    g_pHyprRenderer->drawShadow(shadowBox, rounding, 2.0f, shadowRange, shadowColor, shadowAlpha);
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
