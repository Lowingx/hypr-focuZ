#pragma once

#include "globals.hpp"
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Renderer.hpp>

class CDepthShadowDecoration : public IHyprWindowDecoration {
  public:
    CDepthShadowDecoration(PHLWINDOW window, int depth);
    virtual ~CDepthShadowDecoration() = default;

    virtual SDecorationPositioningInfo getPositioningInfo() override;
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    virtual void                       draw(PHLMONITOR, float const& a) override;
    virtual eDecorationType            getDecorationType() override;
    virtual void                       updateWindow(PHLWINDOW) override;
    virtual void                       damageEntire() override;

    virtual eDecorationLayer           getDecorationLayer() override { return DECORATION_LAYER_BOTTOM; }
    virtual uint64_t                   getDecorationFlags() override { return DECORATION_NON_SOLID; }
    virtual std::string                getDisplayName() override { return "focusZ-depth-shadow"; }

    void setDepth(int depth) { m_depth = depth; }
    int  getDepth() const { return m_depth; }

  private:
    PHLWINDOWREF m_window;
    int          m_depth = 1;

    CBox m_lastBox = {};

    void drawShadow(PHLMONITOR, float const& a);
};
