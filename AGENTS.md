# AGENTS.md

Hyprland plugin (shared library `libfocusZ.so`) implementing a Z-axis depth-focus
layout. C++23. The effect is **layout + opacity only** — no render hook, no
decoration, no window transformer (see ADR-001). Verification is `make check`
(`-Werror`, the CI gate) plus `./run-tests.sh` (18 Google Test cases over the
pure functions); the compositor-side behaviour still needs a live Hyprland
session. GitHub Actions runs both against the `hyprland` Arch package.

## Build & install

- Primary: `make` → `libfocusZ.so`; `make install` → copies to
  `~/.local/share/hyprland/plugins/` (the Makefile's `hyprctl plugins` detection
  always fails — the command needs an arg — so the fallback path is used).
- CMake (`cmake -B build && cmake --build build`) also works but is secondary.
- Tests: `./run-tests.sh` configures `build-tests/` and runs ctest; CI runs
  `make check` then `build-tests && ctest`.
- Deps via pkg-config: `hyprland`, `pixman-1`, `libdrm`. Verified on this
  machine: Hyprland 0.56.2, g++ 16.
- `.gitignore` covers `*.o`, `*.so`, `build/`, `build-tests/`, `.prowl/` —
  build artifacts never appear in git status.

## ABI lock (critical)

The plugin is ABI-locked to the exact Hyprland build it's compiled against.
`PLUGIN_INIT` (src/main.cpp) compares a client hash derived from the version
macros against `__hyprland_api_get_hash()` and throws (unloading itself) on
mismatch. After ANY `hyprland` package upgrade: `make clean && make`, then
reload. This is the top troubleshooting issue in README and issue #6.

## Loading & manual verification

- Generic Hyprland: `plugin = <abs-path>/libfocusZ.so` in `hyprland.conf`.
  Ryoku loads it from a hypr module instead (`hl.plugin.load(...)`), and its
  `focusz.lua` keeps the filename in sync — `make install` writes
  `libfocusZ.so`, so watch that name.
- A new build only takes effect after a session restart: the `.so` stays
  resident in the running Hyprland and `hyprctl plugin unload`/`load` can hand
  back a stale handle.
- No way to test headless. Manual loop: `hyprctl plugin list` to confirm load,
  `hyprctl getoption plugin:focusZ:enabled`, then alt-tab with ≥2 windows on a
  workspace. Config changes need `hyprctl reload`.
- `plugin:focusZ:debug = true` (or `FOCUSZ_DEBUG=1`) writes
  `~/.local/share/hyprland/focusz-debug.log`.

## Config keys

Every key is registered in `PLUGIN_INIT` (src/main.cpp) **and** read at
runtime — keep the two in sync, and add a key only with the code that consumes
it:

`enabled`, `stacking`, `max_layers`, `layer_1_scale`, `layer_2_scale`,
`layer_1_opacity`, `layer_2_opacity`, `card_front_scale`, `card_edge_scatter`,
`card_scatter_reshuffle`, `card_peek_min`, `card_peek_max`, `debug`.

The 18 keys that used to be registered but never read (canvas, frost, borders,
per-layer blur, animation speed, center scale) were deleted in the 2026-10-06
cleanup. If a feature comes back, its keys come back with it.

## Architecture gotchas

- Entry: src/main.cpp — version check, config registration, four EventBus
  listeners (`window.active/open/close/destroy`), the `focusZ:cycle`
  dispatcher (which just delegates to Hyprland's `dispatch cyclenext`), and
  `PLUGIN_EXIT`. There is **no render-stage listener**; adding one back is a
  decision, not a refactor.
- EventBus listeners MUST be stored in the `static SP<CSignalListener>` globals
  (src/main.cpp); losing the reference unregisters the callback.
- `CDepthFocusManager` (src/DepthFocus.cpp) keeps `m_zStack` where index = layer
  depth (0 = focused), anchored to the focused monitor (`m_monitor`) and its
  active workspace, so windows on other monitors/workspaces are untouched.
  Depth is conveyed by **opacity** (alpha var) and by geometry. Do NOT
  reintroduce `renderModif` / `RMOD_TYPE_SCALECENTER` scaling: in Hyprland
  0.56.x `m_renderData.renderModif` is a single global applied to every pass
  element at draw time (`CRenderPass::render`), never reset by
  `beginRender`/`endRender`, and the only reset path is pushed only for
  workspace translate/scale animations. A `SCALECENTER` push in
  `RENDER_PRE_WINDOW` leaked to the focused window, wallpaper/layers/cursor,
  and across frames, corrupting damage tracking and pegging the main thread in
  a render storm that froze the session. Physically resizing tiled windows
  (`sizeAnimation`/`positionAnimation`) is equally fatal: it fights the layout
  engine, oscillates, freezes the session, and double-scales background
  windows. The same applies to attaching an `IWindowTransformer` — its
  `transform()` runs inside `renderWindow` and SEGVed 0.56.2.
- The dead code those crashes produced (`drawCardBorders`, `drawCanvas`,
  `drawCardFrost`, `clipSegment`, `CScaleTransformer`, the render hook, the
  never-attached `CDepthShadowDecoration`) is gone. Do not resurrect a
  placeholder: if a visual effect is wanted, build it for real behind the
  ADR-001 sandbox rule.
- Stacking (src/DepthFocus.cpp `layoutStack`): when `plugin:focusZ:stacking`
  is on, stack windows are floated via `g_layoutManager->changeFloatingMode`
  and positioned with `m_target->setPositionGlobal`, which for a floating
  window applies the box directly (the tiling algorithm can't re-arrange it
  back — that oscillation froze earlier builds). Focus via `cyclenext` does
  NOT raise floating windows (`bringTargetToTop` is a no-op outside groups) and
  the renderer walks `Desktop::windowState()->windows()` in order (later = on
  top), so `layoutStack` calls `raise()` deepest→focused on every stack
  mutation to keep the focused card on top. Restoring un-floats only windows
  WE floated (`floatingManaged` + `!floatingBefore`); user-floated windows are
  left alone. Fullscreen windows are skipped.
- In stacked mode the layer scale lives in the window's **box**, not a render
  transform: the focused card starts at `card_front_scale` (0.72) of the base
  box (workarea minus `MARGIN` 48 px), centered, pinned only once per stint as
  focus (`frontBoxSet`, cleared when the window leaves depth 0). While it stays
  focused the user owns that box (resize/drag) and `layoutStack` reads its live
  geometry and anchors every back card to it, so the deck tracks the focused
  card. Each deeper card is shrunk to `frontBox * getTransformForLayer(i).scale`
  and scattered at an angle/radius hashed from the window address (stable across
  alt-tab), pushed `card_peek_min`..`card_peek_max` past the front footprint so
  it protrudes and stays visible — an opaque focused card covers anything fully
  inside its footprint, which is why earlier "scatter within the pile" builds
  looked unchanged. The clamp keeps every card on-screen.
- Opacity is applied per-window via `pWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)`
  (in 0.56 `alphaTotal()` is the product of all channels, so one channel drives
  final translucency; there is no per-focus alpha channel).
- `max_layers` is 1–16, default 8: the number of *distinct* depth levels.
  Windows beyond it still join the deck at the deepest level (floor: 0.22 scale
  / 0.05 opacity) instead of snapping back to 100%.
- `m_applied` (win addr → `SAppliedState`) is the source of truth for each
  window's depth and float ownership. Nothing is captured or restored beyond
  alpha + float mode; `restoreWindow` unwinds windows evicted past the cap,
  focused away, disabled, or closed.
- `m_depthCache` is rebuilt on stack mutations and the eviction pass in
  `applyAllDepthTransforms` reads it as "still in the stack". It is not a
  render-path cache anymore — there is no render hook.
- Blur is global (`decoration:blur`): the plugin API has no per-window blur
  radius, only an on/off `noblur` rule. Translucent back cards blur whatever is
  behind them for free. Issue #7 tracks that limitation.
- `src/globals.hpp` holds `PHANDLE` + the config-value smart pointers; don't
  introduce parallel access patterns.

## What is deliberately not here

- No per-window blur, no frosted glass, no card borders, no wallpaper canvas,
  no depth shadows — all tracked as issues #16–#18, #24, #7.
- No animation easing: `animation_speed` never did anything and is gone.
- No tests for the compositor-side behaviour; the 18 unit tests cover
  `randSeed`, `clampToWorkarea` and the scale falloff as standalone copies.
  Treat a green test run as necessary, not sufficient.

<!-- prowl-agent -->
## Prowl project context

This repo has a Prowl index of its files, symbols, and how they connect. For any
semantic or structural question -- where code is, what it does, who calls it, or
what a change touches -- **run the read-only prowl CLI first**; do not grep or
read whole files just to locate things. Prowl reindexes what changed before each
query, so answers stay current and are cited to file:line, returned in one call
instead of a grep hit list you then open files to disambiguate.

| Question | First command |
|---|---|
| Map the repository | `prowl overview` |
| Locate a feature or concept | `prowl search "<question>"` |
| Locate a named symbol | `prowl find <name>` |
| Read one symbol's source | `prowl def <name-or-id>` |
| Inspect a file's structure | `prowl outline <path>` |
| Trace who uses a symbol | `prowl references <name-or-id>` |
| Size a change's blast radius | `prowl impact <path>` |
| Inspect uncommitted work | `prowl wip` / `prowl changed` |
| Read a located line range | `prowl peek <file:start-end>` |

Keep grep for exact literal or regex text and glob for filename patterns. CLI
output is token-lean TOON by default; add --format human|toon|json|markdown. If
your harness also wires Prowl as an MCP server, the same index is reachable
there; the CLI needs no server and is the first choice.
<!-- /prowl-agent -->
