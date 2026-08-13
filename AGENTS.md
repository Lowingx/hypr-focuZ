# AGENTS.md

Hyprland plugin (shared library `libfocusZ.so`) implementing a Z-axis depth-focus
layout. C++23. **No tests or linter exist** — the only verification is build + load
in a live Hyprland session. GitHub Actions CI builds against the `hyprland` Arch
package in a container; `make check` rebuilds with `-Werror` to catch warnings
(CI gate), and `make install` is the only local install path.

## Build & install

- Primary: `make` → `libfocusZ.so`; `make install` → copies to
  `~/.local/share/hyprland/plugins/` (Makefile's `hyprctl plugins` detection
  always fails — the command needs an arg — so the fallback path is used).
- CMake (`cmake -B build && cmake --build build`) also works but is secondary.
- Deps via pkg-config: `hyprland`, `pixman-1`, `libdrm`. Verified working on
  this machine: Hyprland 0.56.1, g++ 16.
- `.gitignore` covers `*.o`, `*.so`, `build/` — build artifacts never appear in
  git status.

## ABI lock (critical)

The plugin is ABI-locked to the exact Hyprland build it's compiled against.
`PLUGIN_INIT` (src/main.cpp:38-46) compares `__hyprland_api_get_hash()` against the
running binary's version hash and throws (unloading itself) on mismatch. After
ANY `hyprland` package upgrade: `make clean && make`, then reload. This is the
top troubleshooting issue in README.

## Loading & manual verification

- Loading on this machine: there is no `hyprland.conf` `plugin =` line. The
  Ryoku setup loads the plugin from `~/.config/hypr/modules/focusz.lua`, which
  already points at `hl.plugin.load(home .. "/.local/share/hyprland/plugins/libfocusZ.so")`.
  Generic Hyprland installs use `plugin = <abs-path>/libfocusZ.so` instead.
  The README's load example uses the real repo path (`<repo>/libfocusZ.so`).
- A new build only takes effect after a session restart: the `.so` stays resident
  in the running Hyprland and `hyprctl plugin unload`/`load` can hand back a
  stale handle. `make install` + restart the session (start-hyprland respawns
  Hyprland automatically).
- All options live under `plugin:focusZ:` and are registered by name in
  src/main.cpp via `makeConfigValue` (`enabled`, `stacking`, `max_layers`,
  `layer_1_scale`, `layer_1_opacity`, `layer_1_blur`, `layer_2_*`,
  `animation_speed`, `wallpaper_dim`, `wallpaper_zoom`, `canvas_zoom_floor`,
  `canvas_dim_floor`, `canvas_plate`, `canvas_plate_alpha`,
  `canvas_plate_alpha_max`, `canvas_shadow_boost`, `center_scale`). These names must stay in
  sync between src/main.cpp and hyprland.conf.
- No way to test headless. Manual loop: `hyprctl plugin list` to confirm load,
  `hyprctl getoption plugin:focusZ:enabled`, then alt-tab with ≥2 windows on a
  workspace. After disabling, `hyprctl reload` recalcs window positions.
- Config changes need a `hyprctl reload` (or session restart) to take effect.

## Architecture gotchas

- Entry: src/main.cpp — version check, config registration, EventBus listeners,
  `focusZ:cycle` dispatcher (which just delegates to Hyprland's
  `dispatch cyclenext`).
- EventBus listeners MUST be stored in the `static SP<CSignalListener>` globals
  (src/main.cpp:20-24); losing the reference unregisters the callback.
- `CDepthFocusManager` (src/DepthFocus.cpp) keeps `m_zStack` where index = layer
  depth (0 = focused), anchored to the focused monitor (`m_monitor`) and its
  active workspace, so windows on other monitors/workspaces are untouched. Depth
  is conveyed by **opacity** (alpha var), the `CDepthShadowDecoration`, and the
  `CScaleTransformer` (per-window scale). Do NOT reintroduce `renderModif` /
  `RMOD_TYPE_SCALECENTER` scaling: in Hyprland
  0.56.1 `m_renderData.renderModif` is a single global applied to every pass
  element at draw time (`CRenderPass::render`, Renderer.cpp:187-195, runs after
  all RENDER_* hooks of the walk), is never reset by `beginRender`/`endRender`,
  and the only reset path (`CRendererHintsPassElement::draw`,
  ElementRenderer.cpp:190-194) is pushed only for workspace translate/scale
  animations. A `SCALECENTER` push in `RENDER_PRE_WINDOW` leaked to the focused
  window, wallpaper/layers/cursor, and across frames, corrupting damage tracking
  and pegging the main thread in a render storm that froze the session. Geometry
  must not be resized either (`sizeAnimation`/`positionAnimation`): physically
  resizing tiled windows fights Hyprland's layout engine (which re-arranges them
  back every frame), causing a main-thread oscillation that froze the whole
  session — and it double-scaled background windows (physical * render).
- Hot-path cost is the design invariant: the render hook reads depth from
  `m_depthCache` (O(1), rebuilt only on stack mutations), and
  `applyAllDepthTransforms` skips windows whose depth is unchanged so they are
  not re-damaged. Keep it that way — do not reintroduce scans or unconditional
  damage on the per-frame path.
- `m_applied` (win addr -> `SAppliedState`) is the single source of truth for
  each window's depth and decoration presence. No geometry or render data is
  mutated, so nothing is captured or restored. `restoreWindow` is what
  unwinds windows evicted past the hard cap, focused, disabled, or closed —
  including removing the `CDepthShadowDecoration` (which is otherwise never
  removed, and used to keep drawing on windows that returned to layer 0).
- `layer_1_scale` / `layer_2_scale` are consumed by `CScaleTransformer`
  (src/ScaleTransformer.cpp), a `Render::IWindowTransformer` attached per-window
  (`setWindowScale` in DepthFocus.cpp). It's the only render-safe per-window
  transform in 0.56.1: the window renders into its own fb at `transform()`, which
  is blitted back 1:1 by `drawTransformedWindow` (ElementRenderer.cpp:501-624).
  Depth 0 (focused) and restored windows drop the transformer to stay on the
  cheap direct path. `center_scale` is still registered but inert.
- Stacking (src/DepthFocus.cpp `layoutStack`): when `plugin:focusZ:stacking` is
  on, stack windows are floated via `g_layoutManager->changeFloatingMode`
  (LayoutManager.cpp:76 → `space->toggleTargetFloating` → `m_algorithm->setFloating`
  → `recalculate()`) and positioned with `m_target->setPositionGlobal`. This is
  safe against the tiled-window oscillation above because `CWindowTarget::updatePos`
  applies a floating window's box directly (`setBox(m_box.logicalBox)`), never
  through the tiling algorithm. IMPORTANT: focus via `cyclenext` does NOT raise
  floating windows — `bringTargetToTop` is a no-op outside groups — and the
  renderer walks `Desktop::windowState()->windows()` in order (later = on top),
  so `layoutStack` calls `Desktop::windowState()->raise()` deepest→focused every
  stack mutation to keep the focused window on top. Restoring un-floats only
  windows WE floated (`SAppliedState::floatingManaged` + `floatingBefore`);
  user-floated windows are left alone. Windows are anchored to the monitor's
  active workspace only (`rebuildStack` filters `m_workspace ==
  anchor->m_activeWorkspace`), so off-workspace windows are never floated.
  Fullscreen windows are skipped.
- In stacked mode the layer scale is carried by the window's **box**, not the
  `CScaleTransformer`: the focused card starts at `FRONT_SCALE` (0.70) of the
  base box (workarea minus `MARGIN` 48 px), centered — and is pinned **only
  once per stint** (`SAppliedState::frontBoxSet`, reset whenever a window leaves
  depth 0 in `applyDepthToWindow`). While it stays focused, the user owns the
  front window's box (resizable/draggable): `layoutStack` reads its live
  geometry via `w->m_target->position()` (→ `m_box.logicalBox`) and anchors
  every back card to that box, so the deck always tracks the focused card. Each
  deeper card is shrunk to `frontBox * getTransformForLayer(i).scale` and
  scattered at a random angle/radius (both hashed from the window address, stable
  across focus changes) around the front card, pushed `PEEK_MIN`..`PEEK_MAX`
  (20–40 px) past its footprint so every card protrudes and stays visible behind
  the opaque front one. The safety net keeps every card on-screen. `MARGIN` is larger
  than `PEEK_MAX` so cards stay on-screen without clamping. `applyDepthToWindow`
  therefore drops the transformer (`setWindowScale(pWindow, 1.0f)`) while
  stacking is on — keeping it would double-scale (box × render). The
  `m_stackingActive` member detects a runtime `stacking` toggle in
  `applyAllDepthTransforms`: turning off restores every stack window and clears
  the stack; turning on rebuilds it.
- Opacity is applied per-window via `pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)`
  (Hyprland 0.56: `CWindow::opaque()` returns false when this channel != 1, and
  `alphaTotal()` = product of all alpha channels, so a single channel set does
  drive final translucency for back windows; there is no per-focus alpha channel
  in this version). Back cards MUST protrude past the focused card to be seen —
  an opaque focused card covers any card fully inside its footprint, which is
  why the earlier "scatter within the pile area" builds looked unchanged and
  showed no blur.
- `max_layers` is 1–16, default 8. This controls the number of *distinct* depth
  levels; windows beyond the limit still join the deck at the deepest level
  (floor: 0.22 scale / 0.05 opacity) instead of snapping back to 100%.
- `layer_1_blur` / `layer_2_blur` config values are registered for config
  compatibility but **inert**: the plugin API only has an on/off `noblur` window
  rule, no per-window blur radius, so `getTransformForLayer` intentionally does
  not read them. "Blur behind" is free: global `decoration:blur:enabled` is on
  on this machine, and the Ryoku module `~/.config/hypr/modules/focusz.lua`
  overrides `decoration:blur` to a strong clean profile (size 30, passes 8,
  noise 0.0) so the translucent background cards blur what's behind them deeply
  without grain; the focused window is opaque and stays crisp. Front card starts
  at `FRONT_SCALE` (0.70) of the base box — keep the back-card corners clamped to
  the workarea so cards never run off-screen.
- The wallpaper is pulled back into the depth scene via `pullWallpaper` (called
  from `layoutStack`): the first mapped surface in
  `monitor->m_layerSurfaceLayers[0]` (BACKGROUND layer, matched by layer, not
  namespace — swww/hyprpaper/mpvpaper all differ) gets its `LS_ALPHA_FADE`
  channel set to `plugin:focusZ:wallpaper_dim` (default 0.5 — darker than the
  earlier hardcoded 0.65 so the wallpaper reads as a canvas behind the deck)
  while the stack is live, restored to 1.0 when the stack is cleared/disabled.
  Geometry is deliberately NOT touched:
  `arrangeLayerArray` (Renderer.cpp:2556) re-derives `m_geometry` from the
  client's `desiredSize` on every arrange and `configure()`s the client — any
  scale/box we set would be overwritten (and could resize-loop). Alpha is the
  only safe channel.
- `CDepthShadowDecoration` (src/DepthShadow.cpp) is an `IHyprWindowDecoration`
  subclass drawing the depth-varying shadow.
- `src/globals.hpp` holds `PHANDLE` + shared config-value smart pointers consumed
  everywhere; don't introduce parallel access patterns.

## Canvas read: wallpaper zoom + frosted plate (shipped 2026-08-07)

Two mechanisms turn the wallpaper + deck into a receding-canvas scene:

- **`plugin:focusZ:wallpaper_zoom`** (float 0.5–1.0, default 0.88): at
  `RENDER_POST_WALLPAPER` (onRenderStage → `drawZoomedWallpaper`,
  DepthFocus.cpp) the plugin pushes two pass elements — an **opaque black
  `CClearPassElement`** over the whole monitor, then a **`CTexPassElement`**
  of the wallpaper scaled to `zoom`, centered. The clear is safe because
  `renderBackground` + the BACKGROUND layer surfaces are queued into the SAME
  pass BEFORE the stage emit (Renderer.cpp:1161-1169), so it deterministically
  erases the compositor's bg + the full-screen wallpaper element, and our copy
  is the only thing left under the windows — no reliance on fb pre-clear state,
  and no leak to later elements (a translucent tex is never occlusion-culled).
  The tex element re-applies the dim by reading the layer's live
  `LS_ALPHA_FADE` (`ls->alpha().get(LS_ALPHA_FADE)->value()`), because the tex
  path bypasses the layer alpha channel — keep it in sync with `pullWallpaper`.
  The wallpaper surface texture comes from
  `ls->wlSurface()->resource()->m_current.texture` + `m_current.size` (NOT the
  fallback `pMonitor->m_background`). Boxes are built from
  `pMonitor->m_transformedSize` (projection space, matches renderBackground).
  **Side effect:** the opaque clear erases every other BACKGROUND-layer surface
  (the quickshell 1x1 pill-inhibit helper etc.) while the deck is live — they
  are invisible, so accepted. The zoom only fires while the deck is live on the
  monitor being rendered (stacking on, `m_monitor` == current monitor, stack
  non-empty, wallpaper mapped + textured) and is skipped at `zoom >= 0.995`.
- **`plugin:focusZ:canvas_plate`** (bool, default true): the deepest card in
  the stack becomes a full-workarea translucent matte — `layoutStack` gives it
  `logicalBoxMinusReserved()` instead of the scatter, and
  `applyAllDepthTransforms`/`applyDepthToWindow` pin its alpha to the floor
  (0.05) regardless of its depth (a 2-window stack would otherwise render a
  0.5-alpha plate). The frost comes from the global blur the translucent
  non-opaque window triggers BEHIND it across the whole workarea — the plate's
  own content is almost invisible and that is intended. Plate role is tracked
  in `SAppliedState::plate` and compared in the apply-skip check, because the
  deepest card can become/stop being the plate WITHOUT a depth change; promote
  logic is untouched (plate card is depth>0, restored like any back card).
- Validation loop (manual, no restart needed after `hyprctl reload`):
  `hyprctl getoption plugin:focusZ:wallpaper_zoom`, toggle
  `hyprctl eval 'hl.config({ ["plugin.focusZ.wallpaper_zoom"] = 1.0 })'` to see
  the canvas/stage disappear, alt-tab to watch the plate role pass to the new
  deepest card. A NEW plugin build still needs a session restart (ABI lock).

## Progressive canvas (shipped 2026-08-07)

The canvas is not a fixed one-shot recession: it deepens with every window in
the deck. `CDepthFocusManager::getDeckFactor()` returns how full the deck is
relative to `max_layers` (0.0 with just the focused window, 1.0 once the stack
reaches `max_layers`); each window past the first adds an equal share. That
factor drives all three layers at once in `drawCanvas()` and the shadows:

- **zoom** walks `wallpaper_zoom` (base, 1-window deck) → `canvas_zoom_floor`
  (default 0.55) — the wallpaper recedes further with each window.
- **dim** walks `wallpaper_dim` (0.5) → `canvas_dim_floor` (default 0.15) — the
  canvas darkens with each window.
- **plate alpha** walks `canvas_plate_alpha` (0.05) → `canvas_plate_alpha_max`
  (0.25) — the frosted matte thickens with each window. NOTE the doc above is
  stale on the plate: `drawCanvas` (RENDER_PRE_WINDOWS) now draws a plain
  `CRectPassElement` alpha matte (no live blur — a fullscreen rect blur painted
  the canvas black and was removed; plate role is NOT `SAppliedState::plate`,
  that field no longer exists).
- **shadows** (`CDepthShadowDecoration::drawShadow`) multiply their base
  range/alpha by `1 + (canvas_shadow_boost - 1) * factor` (default boost 1.5x)
  — cards cast deeper shadows as the deck fills. `setDeckFactor` is re-propagated
  on every `applyAllDepthTransforms`, because the factor changes when windows
  open/close even for cards whose layer depth stays the same.


