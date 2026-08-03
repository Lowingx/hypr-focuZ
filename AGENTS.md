# AGENTS.md

Hyprland plugin (shared library `libfocusZ.so`) implementing a Z-axis depth-focus
layout. C++23. **No tests, CI, or linter exist** — the only verification is
build + load in a live Hyprland session.

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
  Ryoku setup loads the plugin from `~/.config/hypr/modules/focusz.lua` via
  `hl.plugin.load("~/.local/share/hyprland/plugins/focusZ.so")`. Generic
  Hyprland installs use `plugin = <abs-path>/libfocusZ.so` instead.
- Filename gotcha: `make install` copies `libfocusZ.so` into
  `~/.local/share/hyprland/plugins/`, but the Ryoku module loads `focusZ.so` (an
  older build). After installing the rewritten engine, point the module at
  `libfocusZ.so` (or rename the installed file), then reload the session. The
  README's load example now uses the real repo path (`<repo>/libfocusZ.so`), not
  the stale `/home/one/hyprland-focusZ/...`.
- All options live under `plugin:focusZ:` and are registered by name in
  src/main.cpp via `makeConfigValue` (`enabled`, `max_layers`, `layer_1_scale`,
  `layer_1_opacity`, `layer_1_blur`, `layer_2_*`, `animation_speed`,
  `center_scale`). These names must stay in sync between src/main.cpp and
  hyprland.conf.
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
  depth (0 = focused), anchored to the focused monitor (`m_monitor`) so other
  monitors are untouched. Depth is conveyed purely by **opacity** (alpha var) and
  the `CDepthShadowDecoration` — the only mutations ever applied to windows. Do
  NOT reintroduce `renderModif` / `RMOD_TYPE_SCALECENTER` scaling: in Hyprland
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
  unwinds windows evicted past `max_layers`, focused, disabled, or closed —
  including removing the `CDepthShadowDecoration` (which is otherwise never
  removed, and used to keep drawing on windows that returned to layer 0).
- `layer_1_scale` / `layer_2_scale` / `center_scale` config values are registered
  for config compatibility but **inert**: per-window visual scaling is impossible
  via `renderModif` (see the gotcha above), so `getTransformForLayer` still
  computes a scale but nothing consumes it.
- `layer_1_blur` / `layer_2_blur` config values are registered for config
  compatibility but **inert**: the plugin API only has an on/off `noblur` window
  rule, no per-window blur radius, so `getTransformForLayer` intentionally does
  not read them.
- `CDepthShadowDecoration` (src/DepthShadow.cpp) is an `IHyprWindowDecoration`
  subclass drawing the depth-varying shadow.
- `src/globals.hpp` holds `PHANDLE` + shared config-value smart pointers consumed
  everywhere; don't introduce parallel access patterns.
