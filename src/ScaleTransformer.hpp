#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>

// Per-window render transformer that scales the window content around its own
// center before it is composited back to the main framebuffer. This is the
// official Hyprland mechanism for per-window visual transforms: when a window
// has a transformer attached, Hyprland renders it to a separate framebuffer,
// calls transform(), and blits the result back 1:1 (ElementRenderer.cpp,
// drawTransformedWindow). Depth is conveyed here purely by a scale factor —
// no renderModif, no geometry mutation.
class CScaleTransformer : public Render::IWindowTransformer {
  public:
    CScaleTransformer(PHLWINDOW pWindow, float scale);
    ~CScaleTransformer() override = default;

    void setScale(float scale);

    SP<Render::IFramebuffer> transform(SP<Render::IFramebuffer> in) override;

  private:
    PHLWINDOWREF m_window;
    float        m_scale;
    SP<Render::IFramebuffer> m_outFB;
};
