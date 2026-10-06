# ADR-001: Render-Path-Free Architecture

- **Status**: accepted
- **Date**: 2026-08-12
- **Deciders**: Lowingx
- **Tags**: safety, performance, hyprland-plugin, render-path

## Context

The focusZ plugin originally used Hyprland's render path (`addPassElement`, `IWindowTransformer`) to apply visual depth effects (card borders, frost, scaling). On Hyprland v0.56.2, these render-path APIs cause SEGV crashes during `renderWindow` — the backtrace lands in `libfocusZ.so`. The crash occurs when monitors are re-leased/modeset while windows open, corrupting `m_renderData.pMonitor`.

The plugin must be safe to load in a production compositor without risking session crashes.

## Decision

Adopt a **render-path-free architecture**: the plugin never calls `addPassElement`, never attaches `IWindowTransformer`, and never registers render-stage drawing code. All visual depth effects are conveyed through:

1. **Per-window opacity** — `pWindow->alpha()` (safe on tiled and floating windows)
2. **Per-card geometry** — `setPositionGlobal()` with scaled bounding boxes
3. **Floating mode** — `changeFloatingMode()` to own card positioning
4. **Window raising** — `Desktop::windowState()->raise()` for Z-ordering

The render hook (`onRenderStage`), the decoration class
(`CDepthShadowDecoration`) and the `CScaleTransformer` are removed outright —
placeholders that are never reached are not a safety mechanism, they are dead
code (removed 2026-10-06).

## Consequences

### Positive

- **Zero crash risk from render path** — the plugin cannot SEGV inside Hyprland's renderWindow
- **Simpler codebase** — removed ~240 lines of dead render code (ScaleTransformer, drawCardBorders, drawCanvas, drawCardFrost)
- **Better performance** — no per-frame render hooks, no framebuffer allocations, no GPU passes
- **Easier maintenance** — no dependency on unstable render API internals
- **CI-enforced** — render-path check (`grep -rn "addPassElement|IWindowTransformer"`) runs on every push

### Negative

- **No per-pixel effects** — cannot do frosted glass blur, card border rendering, or canvas dimming at the render level
- **Visual parity gap** — early frames (000-055) of the target PNGs show flat pre-depth state that requires render-path effects to match exactly
- **Relies on Ryoku's native decorations** — card borders/shadows depend on Hyprland's `decoration:shadow` range, not the plugin

### Neutral

- The 18 inert config keys (canvas, borders, frost, blur, animation speed, center scale) were **removed** instead of kept registered: a key nobody reads is a lie to the user. Reintroduce them together with the feature.
- Future render-path revival must go through a sandboxed test harness before merging

## Links

- `.github/workflows/build.yml` — CI render-path check
- Removed by: `refactor: remove code that exists but never runs`
