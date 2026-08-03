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
`PLUGIN_INIT` (main.cpp:38-46) compares `__hyprland_api_get_hash()` against the
running binary's version hash and throws (unloading itself) on mismatch. After
ANY `hyprland` package upgrade: `make clean && make`, then reload. This is the
top troubleshooting issue in README.

## Loading & manual verification

- Config: `plugin = <abs-path>/libfocusZ.so` in `~/.config/hypr/hyprland.conf`.
  The README's example load path is stale (`/home/one/hyprland-focusZ/...`); use
  the real repo path.
- All options live under `plugin:focusZ:` and are registered by name in
  main.cpp via `makeConfigValue` (`enabled`, `max_layers`, `layer_1_scale`,
  `layer_1_opacity`, `layer_1_blur`, `layer_2_*`, `animation_speed`,
  `center_scale`). These names must stay in sync between main.cpp and
  hyprland.conf.
- No way to test headless. Manual loop: `hyprctl plugin list` to confirm load,
  `hyprctl getoption plugin:focusZ:enabled`, then alt-tab with ≥2 windows on a
  workspace. After disabling, `hyprctl reload` recalcs window positions.
- Config changes need a hyprland.conf reload or session restart.

## Architecture gotchas

- Entry: main.cpp — version check, config registration, EventBus listeners,
  `focusZ:cycle` dispatcher (which just delegates to Hyprland's
  `dispatch cyclenext`).
- EventBus listeners MUST be stored in the `static SP<CSignalListener>` globals
  (main.cpp:20-24); losing the reference unregisters the callback.
- `CDepthFocusManager` (DepthFocus.cpp) keeps `m_zStack` where index = layer
  depth (0 = focused), anchored to the focused monitor (`m_monitor`) so other
  monitors are untouched. Scale is applied in **two** places that must stay in
  sync: position/size animation goals (`applyDepthToWindow`) and the
  `RENDER_PRE_WINDOW` render-modification hook that pushes
  `RMOD_TYPE_SCALECENTER` (DepthFocus.cpp:340-363). Change one and the visual
  breaks.
- Hot-path cost is the design invariant: the render hook reads depth from
  `m_depthCache` (O(1), rebuilt only on stack mutations), and
  `applyAllDepthTransforms` skips windows whose depth is unchanged so they are
  not re-damaged. Keep it that way — do not reintroduce scans or unconditional
  damage on the per-frame path.
- `m_applied` (win addr -> `SAppliedState`) is the single source of truth for
  each window's depth, decoration presence, and captured unscaled geometry.
  `applyDepthToWindow` captures that geometry on first background touch and
  re-captures only when the layout goal grows beyond `orig * scale`; depth-0
  restore reads it back so scaling never compounds. `restoreWindow` is what
  unwinds windows evicted past `max_layers`, focused, disabled, or closed —
  including removing the `CDepthShadowDecoration` (which is otherwise never
  removed, and used to keep drawing on windows that returned to layer 0).
- `layer_1_blur` / `layer_2_blur` config values are registered for config
  compatibility but **inert**: the plugin API only has an on/off `noblur` window
  rule, no per-window blur radius, so `getTransformForLayer` intentionally does
  not read them.
- `CDepthShadowDecoration` (DepthShadow.cpp) is an `IHyprWindowDecoration`
  subclass drawing the depth-varying shadow.
- `globals.hpp` holds `PHANDLE` + shared config-value smart pointers consumed
  everywhere; don't introduce parallel access patterns.
