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

    // 0.0 with a single-window deck, 1.0 at max_layers — deepens the shadow as
    // the deck fills. Mirrors CDepthFocusManager::getDeckFactor().
    void  setDeckFactor(float f) { m_deckFactor = f; }
    float getDeckFactor() const { return m_deckFactor; }

  private:
    PHLWINDOWREF m_window;
    int          m_depth = 1;
    float        m_deckFactor = 0.0f;

    CBox m_lastBox = {};

    void drawShadow(PHLMONITOR, float const& a);
};
