#include "DepthShadow.hpp"
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprutils/math/Box.hpp>
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

void CDepthShadowDecoration::drawShadow(PHLMONITOR /*pMonitor*/, float const& a) {
    // Card border rendering — render-path-safe (called by decoration system, not addPassElement).
    if (!cfg().cardBorder->value())
        return;

    auto w = m_window.lock();
    if (!valid(w) || !w->m_isMapped)
        return;

    // Get window geometry
    auto box = w->getWindowBoxUnified(0);
    if (box.w <= 0 || box.h <= 0)
        return;

    // Parse border color (0xAARRGGBB format)
    const uint32_t colorRaw = static_cast<uint32_t>(cfg().borderColor->value());
    const float r = ((colorRaw >> 16) & 0xFF) / 255.0f;
    const float g = ((colorRaw >> 8) & 0xFF) / 255.0f;
    const float b = (colorRaw & 0xFF) / 255.0f;
    const float alpha = ((colorRaw >> 24) & 0xFF) / 255.0f;

    const int borderWidth = cfg().borderWidth->value();
    if (borderWidth <= 0)
        return;

    // Draw border using OpenGL directly — this is safe from decoration draw()
    // The decoration system ensures we're in the correct render context
    if (Render::GL::g_pHyprOpenGL) {
        // Create damage region from the window box
        CRegion damage;
        damage.add(box);

        // Render the border as a rectangle outline
        // We draw 4 thin rectangles to form the border
        const CBox topBox = CBox(box.x, box.y, box.w, static_cast<double>(borderWidth));
        const CBox bottomBox = CBox(box.x, box.y + box.h - borderWidth, box.w, static_cast<double>(borderWidth));
        const CBox leftBox = CBox(box.x, box.y + borderWidth, static_cast<double>(borderWidth), box.h - 2.0 * borderWidth);
        const CBox rightBox = CBox(box.x + box.w - borderWidth, box.y + borderWidth, static_cast<double>(borderWidth), box.h - 2.0 * borderWidth);

        CHyprColor color{r, g, b, alpha * a};
        Render::GL::CHyprOpenGLImpl::SRectRenderData rectData;
        rectData.damage = &damage;
        rectData.round = 0;
        rectData.roundingPower = 2.F;
        rectData.blur = false;
        rectData.blurA = 1.F;
        rectData.xray = false;

        Render::GL::g_pHyprOpenGL->renderRect(topBox, color, rectData);
        Render::GL::g_pHyprOpenGL->renderRect(bottomBox, color, rectData);
        Render::GL::g_pHyprOpenGL->renderRect(leftBox, color, rectData);
        Render::GL::g_pHyprOpenGL->renderRect(rightBox, color, rectData);
    }
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
