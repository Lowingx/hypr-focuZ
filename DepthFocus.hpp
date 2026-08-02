#pragma once

#include "globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <vector>
#include <optional>

struct SLayerTransform {
    float scale   = 1.0f;
    float opacity = 1.0f;
    bool  blur    = false;
};

class CDepthFocusManager {
  public:
    CDepthFocusManager();
    ~CDepthFocusManager() = default;

    void onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason reason);
    void onWindowOpen(PHLWINDOW pWindow);
    void onWindowClose(PHLWINDOW pWindow);
    void onRenderStage(eRenderStage stage);

    // Get the current layer depth for a window (0 = focused, -1 = first bg, -2 = second bg, etc.)
    int getLayerDepth(PHLWINDOW pWindow) const;

    // Get the transform parameters for a given layer depth
    SLayerTransform getTransformForLayer(int depth) const;

    // Apply all depth transforms to the current window stack
    void applyAllDepthTransforms();

    // Initialize: scan existing windows and build the stack
    void init();

  private:
    // The Z-stack: index 0 = focused (Layer 0), index 1 = Layer -1, etc.
    std::vector<PHLWINDOWREF> m_zStack;

    // Track which windows we've already applied decorations to
    std::unordered_map<uintptr_t, bool> m_decoratedWindows;

    void rebuildStack();
    void applyDepthToWindow(PHLWINDOW pWindow, int depth);
    void promoteWindow(PHLWINDOW pWindow);
    void demoteAllBelow(PHLWINDOW pWindow);
};
