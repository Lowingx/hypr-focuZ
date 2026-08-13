#include "ScaleTransformer.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

CScaleTransformer::CScaleTransformer(PHLWINDOW pWindow, float scale) : m_window(pWindow), m_scale(scale) {}

void CScaleTransformer::setScale(float scale) {
    m_scale = scale;
}

SP<Render::IFramebuffer> CScaleTransformer::transform(SP<Render::IFramebuffer> in) {
    if (!in || m_scale >= 0.995f)
        return in;

    auto* const renderer = g_pHyprRenderer.get();
    if (!renderer)
        return in;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return in;

    // Use the window's own monitor reference instead of renderer->m_renderData.pMonitor
    // (the latter can be a corrupted WP during the first render of a newly-mapped window).
    const auto PMONITOR = PWINDOW->m_monitor.lock();
    if (!PMONITOR)
        return in;

    CBox winBox = PWINDOW->getFullWindowBoundingBox();
    winBox.translate((PWINDOW->m_pinned ? Vector2D{} :
                      (PWINDOW->m_workspace ? PWINDOW->m_workspace->m_renderOffset->value() : Vector2D{})) +
                     PWINDOW->m_floatingOffset - PMONITOR->m_position);
    winBox.scale(PMONITOR->m_scale).round();

    if (winBox.empty())
        return in;

    // Output fb mirrors the input (full-monitor) fb so box coordinates map 1:1.
    if (!m_outFB || m_outFB->m_size != in->m_size) {
        m_outFB = renderer->createFB("focusZ scale");
        if (!m_outFB)
            return in;
        m_outFB->alloc((int)in->m_size.x, (int)in->m_size.y);
    }

    CBox dest = winBox.copy();
    dest.scaleFromCenter(m_scale);

    auto guard = renderer->bindTempFB(m_outFB);

    const auto oldProjection = renderer->m_renderData.projectionType;
    const auto oldFbSize     = renderer->m_renderData.fbSize;

    renderer->setProjectionType(in->m_size);
    renderer->setViewport(0, 0, (int)in->m_size.x, (int)in->m_size.y);

    renderer->draw(CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});

    CRegion                      fullDamage{0, 0, in->m_size.x, in->m_size.y};
    CTexPassElement::SRenderData texData;
    texData.tex    = in->getTexture();
    texData.box    = dest;
    texData.damage = fullDamage;
    renderer->draw(texData, fullDamage);

    renderer->m_renderData.fbSize = oldFbSize;
    renderer->setProjectionType(oldProjection);
    renderer->setViewport(0, 0, (int)PMONITOR->m_pixelSize.x, (int)PMONITOR->m_pixelSize.y);

    return m_outFB;
}
