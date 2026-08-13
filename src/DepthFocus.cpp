#include "DepthFocus.hpp"
#include "DepthShadow.hpp"
#include "DebugLog.hpp"
#include "ScaleTransformer.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

static std::string winStr(PHLWINDOW pWindow) {
    return pWindow ? "0x" + std::to_string((uintptr_t)pWindow.get()) : "(null)";
}

// Attach/update/remove the scale transformer for a window.
//
// DISABLED: the transformer's transform() runs inside Hyprland's renderWindow
// path and SEGVs on Hyprland v0.56.2 (see hyprlandCrashReport*.txt — the
// backtrace lands in libfocusZ.so during renderWindow). We never attach it
// now; the depth scale is conveyed purely by layoutStack()'s per-card geometry
// (back cards get smaller boxes) plus per-window opacity, both safe. This keeps
// the plugin render-path-free so it can no longer crash the compositor.
static void setWindowScale(PHLWINDOW pWindow, float /*scale*/) {
    if (!valid(pWindow))
        return;
    std::erase_if(pWindow->m_transformers, [](const auto& up) {
        return dynamic_cast<CScaleTransformer*>(up.get()) != nullptr;
    });
}

// Sync the debug logger with the plugin:focusZ:debug config value (runtime toggle).
static void syncDebug() {
    DebugLog::setEnabled(cfg().debug->value());
}

CDepthFocusManager::CDepthFocusManager() {
    // Do NOT call init() here — config values are not ready during PLUGIN_INIT.
    // init() will be called on the first event (onWindowOpen or onFocusChange).
    m_needsInit = true;
}

CDepthFocusManager::~CDepthFocusManager() {
    // Restore every tracked window before the plugin tears down: remove the
    // depth shadow, reset alpha to 1.0, drop the scale transformer and unfloat
    // windows we floated. If this is skipped, windows keep a transformer and
    // decoration whose code lives in this .so — the next frame dereferences
    // unmapped memory and the compositor SEGVs (a real crash on plugin unload).
    std::vector<PHLWINDOW> wins;
    for (auto& [addr, st] : m_applied) {
        if (auto w = st.window.lock())
            wins.push_back(w);
    }
    for (auto& w : wins)
        restoreWindow(w);
}

SLayerTransform CDepthFocusManager::getTransformForLayer(int depth) const {
    SLayerTransform t;
    if (depth <= 0) {
        // Layer 0: focused window, full scale
        t.scale   = 1.0f;
        t.opacity = 1.0f;
    } else if (depth == 1) {
        t.scale   = cfg().layer1Scale->value();
        t.opacity = cfg().layer1Opacity->value();
    } else if (depth == 2) {
        t.scale   = cfg().layer2Scale->value();
        t.opacity = cfg().layer2Opacity->value();
    } else {
        // Deeper layers: interpolate from layer 2 down to the floor across the
        // remaining configured depth levels. A fixed per-layer step collapsed to
        // the floor after only ~2 extra layers, so every window past depth 3
        // looked identical — the "blur/dim only happens once" bug. Spreading the
        // falloff over max_layers keeps each deeper card visibly dimmer/smaller.
        const float baseScale   = cfg().layer2Scale->value();
        const float baseOpacity = cfg().layer2Opacity->value();
        const float floorScale  = 0.22f;
        const float floorOpacity = 0.05f;
        const int   maxLayers   = std::max<int>(1, cfg().maxLayers->value());
        const int   extraDepth  = depth - 2;
        const int   totalSteps  = std::max(1, maxLayers - 2); // levels past layer 2
        const float frac        = std::min(1.0f, (float)extraDepth / (float)totalSteps);
        t.scale   = baseScale + (floorScale - baseScale) * frac;
        t.opacity = baseOpacity + (floorOpacity - baseOpacity) * frac;
    }
    return t;
}

float CDepthFocusManager::getDeckFactor() const {
    // Deck "fullness": 0 with just the focused window, 1 once the stack reaches
    // max_layers. Each window past the first adds an equal share.
    const size_t n       = m_zStack.size();
    const int    maxL    = std::max<int>(1, cfg().maxLayers->value());
    const size_t denom   = std::max<size_t>(1, (size_t)maxL - 1);
    return std::clamp((float)(n - 1) / (float)denom, 0.0f, 1.0f);
}

int CDepthFocusManager::getLayerDepth(PHLWINDOW pWindow) const {
    if (!valid(pWindow))
        return -1;

    // O(1) hot path (render hook). The cache is rebuilt on every stack mutation.
    auto it = m_depthCache.find((uintptr_t)pWindow.get());
    if (it != m_depthCache.end())
        return it->second;

    // Fallback scan: covers windows mapped between events.
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
    DebugLog::log("rebuildStack enter");

    // Disabled: unwind every window we touched and drop all state.
    if (!cfg().enabled->value()) {
        for (const auto& ref : m_zStack) {
            auto w = ref.lock();
            if (valid(w))
                restoreWindow(w);
        }
        m_zStack.clear();
        m_applied.clear();
        m_depthCache.clear();
        m_monitor = {};
        m_workspace = {};
        return;
    }

    const auto& windows = Desktop::windowState()->windows();
    const auto focused  = Desktop::focusState()->window();

    // Anchor the stack to the focused window's monitor: background layers stay
    // isolated per workspace/monitor, so windows on other monitors are untouched.
    if (valid(focused))
        m_monitor = focused->m_monitor;
    const auto anchor = m_monitor.lock();
    if (anchor)
        m_workspace = anchor->m_activeWorkspace;

    std::vector<PHLWINDOW> next;
    if (valid(focused))
        next.push_back(focused);

    for (const auto& w : windows) {
        if (!w->m_isMapped || w->isHidden())
            continue;
        if (w == focused)
            continue;
        if (anchor && !(w->m_monitor.lock() == anchor))
            continue;
        // Only the anchor monitor's active workspace joins the stack; windows on
        // other workspaces of the same monitor must not be floated/repositioned.
        if (anchor && w->m_workspace && w->m_workspace != anchor->m_activeWorkspace)
            continue;
        next.push_back(w);
    }

    // The deck holds *every* mapped window on the anchor workspace: deep layers
    // floor at scale/opacity 0.22/0.05, so windows beyond max_layers still join
    // as dim ghosts instead of snapping back to 100% behind the deck. A generous
    // hard cap only guards pathological workspaces.
    constexpr int HARD_CAP = 32;
    if ((int)next.size() > HARD_CAP)
        next.resize(HARD_CAP);

    m_zStack.clear();
    for (auto& w : next)
        m_zStack.emplace_back(w);

    // New deal: back-card spawn positions reshuffle so the scatter reads freshly
    // random (config card_scatter_reshuffle gates whether layoutStack uses it).
    m_dealNonce++;

    DebugLog::log("rebuildStack stack_size=" + std::to_string(m_zStack.size()) + " monitor=" +
                  (m_monitor.lock() ? "set" : "none"));

    applyAllDepthTransforms();
    DebugLog::log("rebuildStack exit");
}

void CDepthFocusManager::onFocusChange(PHLWINDOW pWindow, Desktop::eFocusReason /*reason*/) {
    // Deferred init: config values are ready now (first event after PLUGIN_INIT).
    if (m_needsInit) {
        m_needsInit = false;
        init();
    }

    syncDebug();
    if (!cfg().enabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    DebugLog::log("onFocusChange win=" + winStr(pWindow));

    // Re-anchor whenever the focused window is on a different monitor OR a
    // different workspace of the same monitor. Previously only a monitor change
    // rebuilt the stack, so focusing a window on another workspace promoted it
    // onto the old stack — the deck mixed windows from two workspaces and the
    // dim/blur only updated when clicking the window you wanted to see change.
    const auto anchor = m_monitor.lock();
    if (!anchor || !(pWindow->m_monitor.lock() == anchor) ||
        (pWindow->m_workspace && pWindow->m_workspace != m_workspace.lock())) {
        rebuildStack();
        DebugLog::log("onFocusChange exit (rebuild)");
        return;
    }

    promoteWindow(pWindow);
    applyAllDepthTransforms();
    DebugLog::log("onFocusChange exit");
}

void CDepthFocusManager::onWindowOpen(PHLWINDOW pWindow) {
    // Deferred init: config values are ready now (first event after PLUGIN_INIT).
    if (m_needsInit) {
        m_needsInit = false;
        init();
    }

    syncDebug();
    if (!cfg().enabled->value())
        return;
    if (!valid(pWindow) || !pWindow->m_isMapped)
        return;

    DebugLog::log("onWindowOpen win=" + winStr(pWindow));

    // Only windows on the anchor monitor join this stack.
    if (m_monitor.lock() && !(pWindow->m_monitor.lock() == m_monitor.lock()))
        return;

    // A window opened on another workspace of the same monitor: re-anchor the
    // deck to that workspace (matches onFocusChange, prevents mixing).
    if (pWindow->m_workspace && pWindow->m_workspace != m_workspace.lock()) {
        rebuildStack();
        DebugLog::log("onWindowOpen exit (rebuild)");
        return;
    }

    // ... and only on the anchor workspace (matches rebuildStack's filter).
    const auto anchor = m_monitor.lock();
    if (anchor && pWindow->m_workspace && pWindow->m_workspace != anchor->m_activeWorkspace)
        return;

    promoteWindow(pWindow);
    applyAllDepthTransforms();
    DebugLog::log("onWindowOpen exit");
}

void CDepthFocusManager::onWindowClose(PHLWINDOW pWindow) {
    syncDebug();
    if (!cfg().enabled->value())
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

    if (!found)
        return;

    DebugLog::log("onWindowClose win=" + winStr(pWindow) + " stack_after=" + std::to_string(m_zStack.size()));

    // The window is gone; drop tracking so restore never touches a dead window.
    m_applied.erase((uintptr_t)pWindow.get());

    if (m_zStack.empty()) {
        m_monitor    = {};
        m_workspace  = {};
    }

    applyAllDepthTransforms();
    DebugLog::log("onWindowClose exit");
}

void CDepthFocusManager::promoteWindow(PHLWINDOW pWindow) {
    // Remove from current position if it exists
    for (auto it = m_zStack.begin(); it != m_zStack.end();) {
        if (it->lock() == pWindow)
            it = m_zStack.erase(it);
        else
            ++it;
    }

    // Insert at front (Layer 0). Like rebuildStack, the deck is not truncated to
    // max_layers — every window joins, deeper ones flooring their transform.
    m_zStack.insert(m_zStack.begin(), pWindow);
    constexpr int HARD_CAP = 32;
    if ((int)m_zStack.size() > HARD_CAP)
        m_zStack.resize(HARD_CAP);

    DebugLog::log("promoteWindow win=" + winStr(pWindow) + " stack_size=" + std::to_string(m_zStack.size()));
}

void CDepthFocusManager::applyAllDepthTransforms() {
    // stacking toggled since last apply: switch modes (float+scale boxes ↔
    // tiled+content-scale), not just re-apply the same windows.
    const bool stackingNow = cfg().stacking->value();
    if (stackingNow != m_stackingActive) {
        const bool turningOff = m_stackingActive && !stackingNow;
        m_stackingActive      = stackingNow;
        if (turningOff) {
            for (const auto& ref : m_zStack) {
                auto w = ref.lock();
                if (valid(w))
                    restoreWindow(w);
            }
            m_zStack.clear();
            m_applied.clear();
            m_depthCache.clear();
            m_monitor    = {};
            m_workspace  = {};
            DebugLog::log("applyAllDepthTransforms stacking off — stack cleared");
            return;
        }
        rebuildStack();
        return;
    }

    refreshDepthCache();

    // Restore windows that left the stack (moved monitors, closed).
    for (auto it = m_applied.begin(); it != m_applied.end();) {
        auto w = it->second.window.lock();
        if (valid(w) && m_depthCache.contains(it->first)) {
            ++it;
            continue;
        }
        if (valid(w))
            restoreWindow(w);
        it = m_applied.erase(it);
    }

    // Apply only what changed; unchanged windows keep their goals and are not re-damaged.
    // Depth is clamped to max_layers so windows beyond the configured levels all
    // share the deepest treatment (their transforms floor at 0.22/0.05 anyway).
    const int    maxLayers = std::max<int>(1, cfg().maxLayers->value());
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;

        const uintptr_t addr = (uintptr_t)w.get();
        auto            it   = m_applied.find(addr);
        const int       depth = std::min<int>((int)i, maxLayers - 1);
        if (it != m_applied.end() && it->second.depth == depth)
            continue;

        applyDepthToWindow(w, depth);
    }

    // Arrange the stack as overlapping floating cards (focused on top).
    layoutStack();

    // Propagate the deck factor to every depth shadow: it changes when windows
    // open/close even for cards whose layer depth stays the same.
    const float deckFactor = getDeckFactor();
    for (auto& [addr, st] : m_applied) {
        auto w = st.window.lock();
        if (!valid(w) || !st.decorated)
            continue;
        for (const auto& deco : w->m_windowDecorations) {
            auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
            if (shadowDeco) {
                shadowDeco->setDeckFactor(deckFactor);
                break;
            }
        }
    }

    DebugLog::log("applyAllDepthTransforms exit (applied=" + std::to_string(m_applied.size()) +
                  " stack=" + std::to_string(m_zStack.size()) + ")");
}

void CDepthFocusManager::applyDepthToWindow(PHLWINDOW pWindow, int depth) {
    if (!valid(pWindow))
        return;

    const uintptr_t addr = (uintptr_t)pWindow.get();
    const auto      transform = getTransformForLayer(depth);

    auto& state = m_applied[addr];
    state.window = pWindow;
    state.depth  = depth;

    // A stint as a back card pins the window's box (front × layer scale); the
    // next time it is promoted to front, layoutStack re-applies the default
    // front box (see frontBoxSet).
    if (depth != 0)
        state.frontBoxSet = false;

    DebugLog::log("applyDepthToWindow win=" + winStr(pWindow) + " depth=" + std::to_string(depth) +
                  " opacity=" + std::to_string(transform.opacity) + " scale=" + std::to_string(transform.scale));

    // Opacity is a pure render property — safe on tiled and floating windows alike.
    auto& alphaVar = pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE);
    *alphaVar = transform.opacity;

    const bool stacking = cfg().stacking->value();
    setWindowScale(pWindow, stacking ? 1.0f : transform.scale);

    // Depth shadow decoration is a no-op placeholder (its draw() is empty) and
    // still runs on Hyprland's renderWindow path; we don't attach it anymore so
    // the plugin stays fully render-path-free and cannot crash the compositor.
    state.decorated = true;

    g_pHyprRenderer->damageWindow(pWindow, true);
}

// Draw the receded canvas at RENDER_PRE_WINDOWS, i.e. AFTER the compositor has
// queued the monitor background, the BACKGROUND layer (wallpaper) and the BOTTOM
// layer (frame rails, visualizer, desktop widgets) — every surface of the
// canvas plane — but BEFORE any window. We repaint that whole plane:
//
//   1. an opaque black stage (erases the compositor's own queued surfaces),
//   2. every BACKGROUND/BOTTOM surface redrawn scaled to `wallpaper_zoom`,
//      positioned around the monitor center so the canvas recedes as one, and
//   3. a full-workarea frosted plate (translucent matte + blur) when
//      `canvas_plate` is on — the "table" the deck sits on.
//
// Each surface keeps its live LS_ALPHA_FADE (so client-side dimming still
// applies) times the canvas dim, so wallpaper and widgets recede together. The
// deck's cards render on top of this plane; TOP/OVERLAY layers (notifications,
// OSD, launcher) are transient UI and deliberately stay in the foreground.

void CDepthFocusManager::drawCanvas() {
}

// Clip an axis-aligned segment (one of a card's four edges, box of thickness 0)
// against an occluder box, pushing the visible sub-segment(s) into `out`.
static void clipSegment(const CBox& edge, const CBox& occluder, std::vector<CBox>& out) {
    if (occluder.w <= 0 || occluder.h <= 0) {
        out.push_back(edge);
        return;
    }
    const bool   horizontal = edge.h == 0;
    const double L = occluder.x, R = occluder.x + occluder.w;
    const double T = occluder.y, B = occluder.y + occluder.h;
    if (horizontal) {
        const double y = edge.y;
        if (y < T || y >= B) {
            out.push_back(edge);
            return;
        }
        // No overlap along the edge's own direction (segment was already
        // shortened by an earlier occluder): leave it untouched. Without this,
        // a clip against a box that sits wholly to one side would re-expand
        // the segment past its current bounds and paint over the front card.
        if (edge.x + edge.w <= L || edge.x >= R) {
            out.push_back(edge);
            return;
        }
        const double a = std::max(edge.x, L);
        const double b = std::min(edge.x + edge.w, R);
        if (a > edge.x)
            out.push_back(CBox{edge.x, y, a - edge.x, 0});
        if (b < edge.x + edge.w)
            out.push_back(CBox{b, y, edge.x + edge.w - b, 0});
    } else {
        const double x = edge.x;
        if (x < L || x >= R) {
            out.push_back(edge);
            return;
        }
        // No overlap along the edge's own direction (segment was already
        // shortened by an earlier occluder): leave it untouched.
        if (edge.y + edge.h <= T || edge.y >= B) {
            out.push_back(edge);
            return;
        }
        const double a = std::max(edge.y, T);
        const double b = std::min(edge.y + edge.h, B);
        if (a > edge.y)
            out.push_back(CBox{x, edge.y, 0, a - edge.y});
        if (b < edge.y + edge.h)
            out.push_back(CBox{x, b, 0, edge.y + edge.h - b});
    }
}

// Live target box of every deck card, projected to monitor space (global →
// monitor-local, then × scale, rounded). Invalid/fullscreen cards get an empty
// box so they neither draw nor occlude. Indexed like m_zStack.
std::vector<CBox> CDepthFocusManager::projectedCardBoxes(Monitor::CMonitor* pMonitor, float sc) {
    std::vector<CBox> out(m_zStack.size());
    if (!pMonitor)
        return out;
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w) || Fullscreen::controller()->isFullscreen(w))
            continue;
        CBox winBox = w->m_target->position(); // live box: our setPositionGlobal, or user's drag/resize
        if (winBox.w < 1 || winBox.h < 1)
            continue;
        CBox mb = winBox.translate(-pMonitor->m_position).scale(sc).round();
        if (mb.w >= 1 && mb.h >= 1)
            out[i] = mb;
    }
    return out;
}

// Draw a border around every deck card at RENDER_POST_WINDOWS — after the
// windows queued their surfaces, before TOP/OVERLAY (notifications, OSD,
// launcher) — so the deck reads as distinct stacked cards with a rim. The box
// is the window's live target box converted to monitor projection space,
// exactly like CHyprBorderDecoration (global → monitor-local, then × scale).
// The focused card gets the full rounded rim; back cards get only the rim
// segments that stick out past the focused card (each edge clipped against the
// front box), so no back-card rim ever crosses the front window.
void CDepthFocusManager::drawCardBorders() {
    if (!cfg().stacking->value())
        return;
    if (!cfg().cardBorder->value())
        return;

    const int borderW = cfg().borderWidth->value();
    if (borderW <= 0)
        return;

    if (m_zStack.empty())
        return;

    // Get monitor from focused window — avoids corrupted pMonitor WP.
    auto focused = m_zStack[0].lock();
    if (!valid(focused))
        return;
    auto pMonitor = focused->m_monitor.lock();
    if (!pMonitor)
        return;

    const uint64_t hex = (uint64_t)(cfg().borderColor->value());
    const float    sc  = pMonitor->m_scale;

    // Projected box of every deck card — the occluder set each back card clips
    // against. Deeper cards overlap shallower ones (not just the focused one),
    // so a back card's rim must be clipped against ALL cards above it.
    const auto monBoxes = projectedCardBoxes(pMonitor.get(), sc);

    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;
        // Fullscreen windows keep Hyprland's own fullscreen geometry.
        if (Fullscreen::controller()->isFullscreen(w))
            continue;

        const CBox monBox = monBoxes[i];
        if (monBox.w < 1 || monBox.h < 1)
            continue;

        const int rounding   = (int)std::lround(w->rounding() * sc);
        const auto transform = getTransformForLayer((int)i);
        const float borderA  = i == 0 ? 1.0f : transform.opacity;

        if (i == 0) {
            // Focused card: full rounded rim — nothing occludes the top card.
            CBorderPassElement::SBorderData data;
            data.box           = monBox;
            data.grad1         = Config::CGradientValueData(CHyprColor(hex));
            data.round         = rounding;
            data.outerRound    = (int)std::lround((w->rounding() + borderW) * sc);
            data.roundingPower = 2.0f;
            data.borderSize    = borderW;
            data.a             = borderA;
            data.window        = w;
            g_pHyprRenderer->addPassElement(Hyprutils::Memory::makeUnique<CBorderPassElement>(data));
            continue;
        }

        // Back cards: draw only the rim segments visible past every shallower
        // card. Each of the four edges (trimmed by the card's rounding so the
        // line stops at the rounded corners) is clipped against occluders
        // 0..i-1 and queued as a thin rect.
        CHyprColor  col(hex);
        const double inset  = (double)rounding;
        const double halfW  = borderW / 2.0;
        struct Edge { double x0, y0, x1, y1; bool horizontal; };
        const Edge edges[4] = {
            {monBox.x + inset, monBox.y + halfW, monBox.x + monBox.w - inset, monBox.y + halfW, true},
            {monBox.x + inset, monBox.y + monBox.h - halfW, monBox.x + monBox.w - inset, monBox.y + monBox.h - halfW, true},
            {monBox.x + halfW, monBox.y + inset, monBox.x + halfW, monBox.y + monBox.h - inset, false},
            {monBox.x + monBox.w - halfW, monBox.y + inset, monBox.x + monBox.w - halfW, monBox.y + monBox.h - inset, false},
        };
        for (const auto& e : edges) {
            CBox seg{e.x0, e.y0, e.horizontal ? e.x1 - e.x0 : 0, e.horizontal ? 0 : e.y1 - e.y0};
            if (seg.w <= 0 && seg.h <= 0)
                continue;
            std::vector<CBox> visible{seg};
            for (size_t o = 0; o < i && !visible.empty(); o++) {
                const CBox& occ = monBoxes[o];
                if (occ.w < 1 || occ.h < 1)
                    continue;
                std::vector<CBox> next;
                for (const auto& s : visible)
                    clipSegment(s, occ, next);
                visible.swap(next);
            }
            for (const auto& v : visible) {
                CBox rc = e.horizontal ? CBox{v.x, e.y0 - halfW, v.w, (double)borderW}
                                       : CBox{e.x0 - halfW, v.y, (double)borderW, v.h};
                CRectPassElement::SRectData data;
                data.box           = rc;
                data.color         = CHyprColor(col.r, col.g, col.b, borderA);
                data.round         = 0;
                data.roundingPower = 2.0f;
                g_pHyprRenderer->addPassElement(Hyprutils::Memory::makeUnique<CRectPassElement>(data));
            }
        }
    }
}

// Draw a frosted-glass veil over every back card at RENDER_POST_WINDOWS (queued
// before drawCardBorders so the rims stay on top). Per-window blur radius is not
// part of the Hyprland 0.56 plugin API, so this is the render-side frost: each
// back card gets a translucent pane that samples the SAME precomputed blur FB the
// canvas plate uses (the bright original canvas), mixed at an alpha that grows
// with depth — deeper cards read as recessed behind denser glass, which is what
// sells the Z-axis look alongside the scale/opacity/shadow ladder. Veil alpha is
// gated on card_frost and scaled by card_frost_strength; at depth 1 it is ~0.7x
// the strength, at depth 2 ~0.95x, deeper it floors at the strength itself.
void CDepthFocusManager::drawCardFrost() {
    // Removed: blur is applied globally on the canvas in drawCanvas(),
    // clipped to exclude the focused window and bar. No per-card blur.
}

void CDepthFocusManager::layoutStack() {
    // Stacking off (or not yet initialized): stay tiled; the canvas is not drawn.
    if (!cfg().stacking->value())
        return;

    if (m_zStack.empty())
        return;

    // Get the monitor from the focused window instead of m_monitor (avoids weak pointer crash).
    auto focused = m_zStack[0].lock();
    if (!valid(focused))
        return;
    auto anchor = focused->m_monitor.lock();
    if (!anchor)
        return;

    // Base card = workarea minus a margin; the focused card keeps that box. Back
    // cards are *smaller* (front card × the layer's scale factor) and each one
    // is thrown near a random workarea edge (top/right/bottom/left) so the
    // stack reads as cards spread on a table with the borders visible. Every
    // card protrudes PEEK_MIN..PEEK_MAX px past the front card's footprint,
    // so none is buried. Seeds are hashed from the window address, depth and a
    // per-deal nonce, so positions never jitter across frames but re-deal
    // fresh on every stack rebuild.
    constexpr double MARGIN = 48.0; // front card's inset from the workarea
    // Front card size as a fraction of the base box (config: card_front_scale).
    // Back cards are sized from the FRONT card × layer scale (see the loop), so
    // the cascade reads against the front: 0.72 / 0.45 / 0.30 / … down to the
    // 0.22 floor — each deeper card is a genuinely smaller card.
    const double FRONT_SCALE = cfg().frontScale->value();
    const bool   EDGE        = cfg().edgeScatter->value();
    // Free scatter: minimum visible strip of a back card past the front footprint.
    // Around mode: min/max the card protrudes past the front footprint. Configurable
    // so a small deck can be pulled tight or a big one fanned wide.
    const double PEEK_MIN = cfg().peekMin->value();
    const double PEEK_MAX = cfg().peekMax->value();

    // Deterministic splitmix64 finalizer from a uintptr_t seed.
    auto randSeed = [](uintptr_t seed) -> uint64_t {
        uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        return h;
    };

    CBox base = anchor->logicalBoxMinusReserved();
    base      = CBox{base.x + MARGIN, base.y + MARGIN, base.w - 2 * MARGIN, base.h - 2 * MARGIN};

    // The front card starts at FRONT_SCALE of the base box, centered; while it
    // stays focused the user owns its box (resize/drag) and back cards anchor to
    // its *live* geometry, so the deck always tracks the focused card.
    CBox frontBox = CBox{base.x + base.w * (1.0 - FRONT_SCALE) / 2.0,
                         base.y + base.h * (1.0 - FRONT_SCALE) / 2.0,
                         base.w * FRONT_SCALE,
                         base.h * FRONT_SCALE};

    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (!valid(w))
            continue;

        // Fullscreen windows keep Hyprland's fullscreen geometry.
        if (Fullscreen::controller()->isFullscreen(w))
            continue;

        const uintptr_t addr = (uintptr_t)w.get();
        auto&           st   = m_applied[addr];

        // Floating is required for us to own the box: updatePos() then applies
        // our m_box directly, instead of the tiling algorithm re-arranging the
        // window every frame (the geometry-freeze loop from AGENTS.md).
        if (!w->m_isFloating) {
            if (!st.floatingManaged) {
                st.floatingBefore  = false;
                st.floatingManaged = true;
            }
            g_layoutManager->changeFloatingMode(w->m_target);
        } else if (!st.floatingManaged) {
            // Was already floating before we ever touched it — leave it so on restore.
            st.floatingBefore  = true;
            st.floatingManaged = true;
        }

        if (i == 0) {
            // Focused card: apply the default centered box only once per stint,
            // then hand the box to the user (resizable/draggable) — layoutStack
            // never re-pins it while this window stays focused.
            if (!st.frontBoxSet) {
                w->m_target->setPositionGlobal(frontBox);
                st.frontBoxSet = true;
            }
            // Live geometry of the focused card anchors every back card.
            frontBox = w->m_target->position();
            continue;
        }

        // Back cards scale down from the FRONT card's live size (front × layer
        // scale): the deeper card is a genuinely smaller card, the same cascade
        // as the receding canvas — not a near-twin of the front (which is what
        // sizing from the full workarea base produced).
        const double s  = getTransformForLayer((int)i).scale;
        const double cw = frontBox.w * s;
        const double ch = frontBox.h * s;

        // Scatter seed: hash of the window address mixed with its depth and a
        // per-deal nonce, so (a) a card that moves between layers lands in a new
        // spot, (b) each stack rebuild re-deals the positions (card_scatter_reshuffle
        // off = pure address hash, the old never-changing layout). Stable within a
        // build, so frames never jitter.
        const uint64_t nonceMix =
            (cfg().scatterReshuffle->value()) ? m_dealNonce * 0x9E3779B97F4A7C15ull : 0ull;
        const uint64_t seed = randSeed(addr ^ ((uint64_t)i << 32) ^ nonceMix);

        CBox target;
        if (EDGE) {
            // Edge-bias scatter (card_edge_scatter=true): each back card lands
            // near a random workarea edge (top/right/bottom/left) with a small
            // inset, so the stack reads as cards spread on a table with edges
            // visible. The peek guarantee ensures at least PEEK_MIN px of the
            // card protrudes past the front card. Re-rolls up to 64 times.
            CBox   work      = anchor->logicalBoxMinusReserved();
            CBox   best      = CBox{work.x, work.y, cw, ch};
            double bestPeek  = -1.0;
            constexpr double EDGE_INSET = 16.0; // distance from the screen edge
            for (int attempt = 0; attempt < 64; attempt++) {
                const uint64_t r = randSeed(addr ^ ((uint64_t)i << 32) ^ nonceMix ^
                                            ((uint64_t)attempt * 0x9E3779B97F4A7C15ull));
                const int   edge = (int)(r >> 0) & 0x3;       // 0 top 1 right 2 bottom 3 left
                const double along = (double)((r >> 16) & 0xFFFF) / 65536.0; // 0..1 along the edge
                double tx, ty;
                switch (edge) {
                    case 0: // top
                        tx = work.x + along * std::max(0.0, work.w - cw);
                        ty = work.y + EDGE_INSET;
                        break;
                    case 1: // right
                        tx = work.x + work.w - cw - EDGE_INSET;
                        ty = work.y + along * std::max(0.0, work.h - ch);
                        break;
                    case 2: // bottom
                        tx = work.x + along * std::max(0.0, work.w - cw);
                        ty = work.y + work.h - ch - EDGE_INSET;
                        break;
                    default: // left
                        tx = work.x + EDGE_INSET;
                        ty = work.y + along * std::max(0.0, work.h - ch);
                        break;
                }
                const double exL = frontBox.x - tx;
                const double exR = tx + cw - (frontBox.x + frontBox.w);
                const double exT = frontBox.y - ty;
                const double exB = ty + ch - (frontBox.y + frontBox.h);
                const double peekAmt = std::max(std::max(exL, exR), std::max(exT, exB));
                if (peekAmt > bestPeek) {
                    bestPeek = peekAmt;
                    best     = CBox{tx, ty, cw, ch};
                }
                if (peekAmt >= PEEK_MIN)
                    break;
            }
            target = best;
        } else {
            // Around mode (card_edge_scatter=false): scatter each back card
            // around the front card at a random angle/radius. The offset pushes
            // the card's center past the front card's half-size by
            // PEEK_MIN..PEEK_MAX, so every card protrudes past the opaque front
            // footprint and stays visible.
            const double angle  = (double)((seed >> 0) & 0xFFFF) / 65536.0 * 2.0 * M_PI;
            const double radius = PEEK_MIN + (double)((seed >> 48) & 0xFFFF) / 65536.0 * (PEEK_MAX - PEEK_MIN);
            const double fcx    = frontBox.x + frontBox.w / 2.0;
            const double fcy    = frontBox.y + frontBox.h / 2.0;
            const double ox     = std::cos(angle) * (frontBox.w / 2.0 + radius);
            const double oy     = std::sin(angle) * (frontBox.h / 2.0 + radius);
            target              = CBox{fcx + ox - cw / 2.0, fcy + oy - ch / 2.0, cw, ch};
        }

        // Safety net: keep every card on-screen.
        CBox work = anchor->logicalBoxMinusReserved();
        CBox finalBox = target;
        if (finalBox.x < work.x)
            finalBox.x = work.x;
        if (finalBox.y < work.y)
            finalBox.y = work.y;
        if (finalBox.x + finalBox.w > work.x + work.w)
            finalBox.x = work.x + work.w - finalBox.w;
        if (finalBox.y + finalBox.h > work.y + work.h)
            finalBox.y = work.y + work.h - finalBox.h;
        w->m_target->setPositionGlobal(finalBox);
    }

    // Render order must match the stack. renderWorkspaceWindows walks
    // windowState()->windows() in order (later = on top), and focus changes via
    // cyclenext only call bringTargetToTop (a no-op outside groups) — so raise
    // the stack from deepest to focused to put the focused window on top.
    for (size_t i = m_zStack.size(); i > 0; i--) {
        auto w = m_zStack[i - 1].lock();
        if (valid(w))
            Desktop::windowState()->raise(w);
    }

    DebugLog::log("layoutStack count=" + std::to_string(m_zStack.size()));
}

void CDepthFocusManager::restoreWindow(PHLWINDOW pWindow) {
    if (!valid(pWindow))
        return;

    // Drop the depth shadow decoration if present.
    for (const auto& deco : pWindow->m_windowDecorations) {
        auto* shadowDeco = dynamic_cast<CDepthShadowDecoration*>(deco.get());
        if (shadowDeco) {
            HyprlandAPI::removeWindowDecoration(PHANDLE, shadowDeco);
            break;
        }
    }

    // Reset alpha to full. Geometry is untouched: scaling was render-only.
    *pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE) = 1.0f;

    // Drop any scale transformer: the window leaves the transformed path.
    setWindowScale(pWindow, 1.0f);

    // Return a window we floated back to tiling (if it wasn't floating before).
    if (pWindow->m_isMapped) {
        const auto it = m_applied.find((uintptr_t)pWindow.get());
        if (it != m_applied.end() && it->second.floatingManaged && !it->second.floatingBefore && pWindow->m_isFloating)
            g_layoutManager->changeFloatingMode(pWindow->m_target);
    }

    g_pHyprRenderer->damageWindow(pWindow, true);

    DebugLog::log("restoreWindow win=" + winStr(pWindow));
}

void CDepthFocusManager::refreshDepthCache() {
    const int maxLayers = std::max<int>(1, cfg().maxLayers->value());
    m_depthCache.clear();
    for (size_t i = 0; i < m_zStack.size(); i++) {
        auto w = m_zStack[i].lock();
        if (valid(w))
            m_depthCache[(uintptr_t)w.get()] = std::min<int>((int)i, maxLayers - 1);
    }
}

void CDepthFocusManager::onRenderStage(eRenderStage /*stage*/) {
    // Render hook disabled: drawCardBorders() called g_pHyprRenderer->addPassElement()
    // during RENDER_POST_WINDOWS, which SEGVs inside Hyprland's addPassElement on
    // v0.56.2 (see hyprlandCrashReport*.txt). The deck is done with safe
    // layout + opacity only, so nothing needs to run here.
}
