#pragma once

#include "globals.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <vector>
#include <unordered_map>

namespace Monitor {
class CMonitor;
}

struct SLayerTransform {
    float scale   = 1.0f;
    float opacity = 1.0f;
};

// Per-window bookkeeping so the hot paths (render hook, focus cycles) stay cheap:
// depth lookups are O(1), and re-applying/damaging is skipped when nothing changed.
struct SAppliedState {
    PHLWINDOWREF window;
    int          depth     = -1;
    bool         decorated = false;
    // Stacking: whether this window was floated by us, and its state before that.
    bool floatingBefore  = false;
    bool floatingManaged = false;
    // Whether we have already applied the default front card box for the current
    // stint as the focused window. Once set, the user owns the front window's
    // box (resize/drag) and layoutStack reads its live geometry.
    bool frontBoxSet = false;
};

class CDepthFocusManager {
  public:
    CDepthFocusManager();
    ~CDepthFocusManager();

    void onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason reason);
    void onWindowOpen(PHLWINDOW pWindow);
    void onWindowClose(PHLWINDOW pWindow);
    void onRenderStage(eRenderStage stage);

    // Current layer depth for a window (0 = focused, 1 = first bg, 2 = second bg, ...).
    int getLayerDepth(PHLWINDOW pWindow) const;

    // Transform parameters for a given layer depth.
    SLayerTransform getTransformForLayer(int depth) const;

    // How full the deck is relative to max_layers: 0.0 with a single window,
    // 1.0 once the deck reaches max_layers. Drives the progressive canvas
    // (zoom/dim/plate) and the per-window shadow scaling.
    float getDeckFactor() const;

    // Apply all depth transforms to the current window stack.
    void applyAllDepthTransforms();

    // Initialize: scan existing windows and build the stack.
    void init();

  private:
    // The Z-stack: index 0 = focused (Layer 0), index 1 = Layer 1, etc.
    std::vector<PHLWINDOWREF> m_zStack;

    // The stack is anchored to one monitor so windows on other monitors are untouched.
    PHLMONITORREF m_monitor;

    // The workspace the stack was built for. Focus/opens on a different
    // workspace of the same monitor must rebuild (re-filter) the deck instead of
    // promoting onto stale windows from the previous workspace.
    PHLWORKSPACEREF m_workspace;

    // win addr -> applied state (depth, decoration, original geometry).
    std::unordered_map<uintptr_t, SAppliedState> m_applied;

    // win addr -> depth, rebuilt on stack mutations, O(1) read in the render hook.
    std::unordered_map<uintptr_t, int> m_depthCache;

    // Last-seen value of plugin:focusZ:stacking, so toggles force a re-apply.
    bool m_stackingActive = true;

    // Bumped on every rebuildStack: re-deals the back-card scatter so spawn
    // positions look freshly random instead of permanently glued to a hash of
    // the window address. Zero/off = the old stable-per-window positions.
    uint64_t m_dealNonce = 0;

    // Deferred init: PLUGIN_INIT cannot access config values, so init()
    // is called on the first event instead.
    bool m_needsInit = true;

    void rebuildStack();
    void applyDepthToWindow(PHLWINDOW pWindow, int depth);
    void restoreWindow(PHLWINDOW pWindow);
    void refreshDepthCache();
    void promoteWindow(PHLWINDOW pWindow);
    void layoutStack();
    void ensureFloating(PHLWINDOW w, SAppliedState& st);
    CBox scatterOnEdges(Monitor::CMonitor* anchor, const CBox& frontBox,
                        double cw, double ch, uintptr_t addr, size_t depth,
                        uint64_t nonceMix, double peekMin);
    CBox scatterAroundFocused(const CBox& frontBox, double cw, double ch,
                              uint64_t seed, double peekMin, double peekMax);
    CBox clampToWorkarea(const CBox& target, Monitor::CMonitor* anchor);
};
