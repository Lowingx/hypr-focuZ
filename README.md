# focusZ — Z-Axis Depth Focus Layout for Hyprland

A Hyprland plugin that gives windows real **Z-axis depth**: the focused window sits
on top at full size, and background windows are **stacked behind it as overlapping
cards** that recede with reduced scale, opacity, and a depth-varying shadow.

## Effect

When `stacking` is enabled the stack becomes a pile of floating cards on the
monitor's active workspace. The focused card is resized to 70% of the workarea
(minus a small margin) and centered; every deeper card is **smaller** — the box
is scaled by the layer's scale factor, so the scale is real geometry, not just a
render transform — and is **parked in one of the focused card's four corners**,
pushed diagonally outward toward the screen's extremities (20–40 px, hashed from
the window's address so positions never jitter when you alt-tab). One back card
per corner, so every card is clearly visible behind the front one, and the pile
reads as receding scale, translucency, and blur.

| Layer | Scale | Opacity | Shadow |
|-------|-------|---------|--------|
| 0 (focused) | 1.00 | 1.0 | — (native) |
| 1 (behind)  | 0.62 | 0.5 | Medium |
| 2 (behind)  | 0.42 | 0.2 | Min |
| 3+          | extrapolated (−0.13/layer) | extrapolated (−0.18/layer) | Min |

Blur is **free**: with global `decoration:blur:enabled` on (default), the
translucent background cards blur whatever is behind them — wallpaper, layers,
and each other. The plugin API has no per-window blur radius (only an on/off
`noblur` window rule), so the `layer_1_blur` / `layer_2_blur` toggles are reserved
but inert. The Ryoku module (`~/.config/hypr/modules/focusz.lua`) sets a strong,
clean global blur (size 30, passes 8, noise 0.0) so the backdrop reads as deep
and sharp rather than grainy; the focused window is opaque and stays crisp.

While the deck is live the **wallpaper is pulled back** into the scene: its layer
surface's fade alpha is dimmed to 0.65 so it recedes along the Z axis behind the
cards (geometry is untouched — the compositor owns layer arrangement). When the
stack is cleared or stacking is disabled the wallpaper is restored to full alpha.

Switching focus (`focusZ:cycle` or clicking a card) promotes the window to Layer 0,
re-stacks the pile, and the cards animate to their new positions.

## Requirements

- **Hyprland 0.56.1** (exact version — the plugin is ABI-locked to the running build)
- `hyprland` headers (`hyprland` package on Arch, provides `pkg-config --cflags hyprland`)
- C++23 compiler (`g++` 14+ or `clang++` 18+)
- Build deps: `pixman`, `libdrm`

## Build

```bash
make            # produces libfocusZ.so
# or with CMake:
cmake -B build && cmake --build build
```

## Install

```bash
make install    # copies to ~/.local/share/hyprland/plugins/
```

Then enable it in your Hyprland config:

```hyprlang
plugin = /path/to/hypr-focuZ/libfocusZ.so
```

> On the Ryoku setup this repo is developed on, the plugin is loaded from
> `~/.config/hypr/modules/focusz.lua` (`hl.plugin.load(...)`) rather than a
> `plugin =` line. Note that the module loads `focusZ.so`, while `make install`
> produces `libfocusZ.so` — keep the installed filename and the module path in
> sync after installing.

## Configuration

All options live under `plugin:focusZ:` in your Hyprland config:

```hyprlang
plugin {
    focusZ {
        # Master toggle
        enabled = true

        # Stack windows as overlapping floating cards (true), or keep tiling
        # and apply only scale/opacity/shadow (false)
        stacking = true

        # How many windows participate in the depth stack (1-16)
        max_layers = 8

        # Scale factors per background layer
        layer_1_scale = 0.62      # 0.4 - 1.0
        layer_2_scale = 0.42      # 0.3 - 1.0

        # Opacity per background layer
        layer_1_opacity = 0.5     # 0.1 - 1.0
        layer_2_opacity = 0.2     # 0.1 - 1.0

        # Whether to apply blur to background layers
        # NOTE: reserved — per-window blur radius isn't part of the plugin API;
        # blur follows the global blur:size. These toggles currently do nothing.
        layer_1_blur = true
        layer_2_blur = true

        # Animation speed for depth transitions (higher = snappier)
        # NOTE: reserved — positions animate with Hyprland's native anim system.
        animation_speed = 8.0     # 1.0 - 20.0

        # Scale windows toward monitor center (true) or top-left (false)
        # NOTE: reserved — not currently wired to anything.
        center_scale = true

        # Write a debug log + heartbeat to ~/.local/share/hyprland/focusz-debug.log
        debug = false
    }
}
```

Config changes need a `hyprctl reload` (or session restart) to take effect.

### Keybind

Cycle focus through the depth stack:

```hyprlang
bind = ALT, Tab, focusZ:cycle
```

## How It Works

The plugin hooks into four Hyprland systems:

1. **EventBus listeners** — `window.active`, `window.open`, `window.close` and
   `window.destroy` events maintain an ordered Z-stack of windows. On focus change
   the newly focused window is promoted to Layer 0 and the rest demote. The stack
   is anchored to the focused monitor's **active workspace**, so windows on other
   monitors or other workspaces are never touched.

2. **Per-window render scale** — Each background window gets a
   `CScaleTransformer` (a `Render::IWindowTransformer`). Hyprland renders the
   window into its own framebuffer, `transform()` shrinks it around the window's
   center, and it is blitted back 1:1 — a fully render-safe, per-window scale that
   never fights the layout engine. The focused window and restored windows drop the
   transformer to stay on the cheap direct path.

3. **Stacking as floating cards** — When `stacking` is on, stack windows are
   floated (`changeFloatingMode`) and positioned with `setPositionGlobal`, which
   for a floating window applies the box directly — the tiling algorithm can't
   re-arrange it back (the geometry oscillation that froze earlier builds). The
   renderer draws floating windows in window-list order, and focus via `cyclenext`
   does **not** raise a floating window, so the plugin explicitly raises the stack
   deepest→focused on every mutation to keep the focused card on top. Only windows
   the plugin floated are returned to tiling on restore; user-floated windows are
   left alone. Fullscreen windows are skipped.

4. **Depth shadow** — Background windows receive a `CDepthShadowDecoration`
   (`IHyprWindowDecoration` subclass) whose shadow range/offset varies with the
   layer. It is removed when the window returns to Layer 0 or leaves the stack.

Depth lookups in the render path are O(1) (cached per window), and windows whose
depth didn't change are never re-damaged, so the per-frame cost is flat.

## Architecture

```
src/main.cpp            Plugin entry: PLUGIN_API_VERSION / PLUGIN_INIT / PLUGIN_EXIT
                    Registers config values, EventBus listeners, dispatcher
                    Version-hash check against running Hyprland

src/DepthFocus.hpp/cpp  CDepthFocusManager — the core depth engine
                    Maintains Z-stack, computes per-layer transforms,
                    applies scale transformer, floats + positions the stack

src/ScaleTransformer.cpp/hpp
                    CScaleTransformer — Render::IWindowTransformer
                    Per-window scale via the official transformed-fb pipeline

src/DepthShadow.hpp/cpp CDepthShadowDecoration — IHyprWindowDecoration subclass
                    Draws depth-aware shadows (range/offset vary by layer)

src/DebugLog.cpp/hpp    Optional debug logger + main-thread heartbeat
                    ~/.local/share/hyprland/focusz-debug.log

src/globals.hpp         Plugin handle + config value smart pointers
```

## Troubleshooting

**"Version mismatch" notification on load** — The plugin must be compiled against
the exact same Hyprland headers as the running binary. Rebuild after any Hyprland
update: `make clean && make`.

**A new build doesn't take effect** — The plugin `.so` stays resident in the
running Hyprland; `hyprctl plugin unload`/`load` can hand back a stale handle.
Restart the Hyprland session after `make install` to pick up a fresh build.

**No visual effect** — Check `hyprctl getoption plugin:focusZ:enabled` and
`hyprctl getoption plugin:focusZ:stacking`, and make sure you have ≥2 windows on
the workspace. With `debug = true`, `focusz-debug.log` shows the stack rebuilds
and per-window depth applications.

**Windows went floating** — That's the stacking layout: cards overlap, so the
stack is floated and repositioned by the plugin. Set `stacking = false` to keep
tiling (scale/opacity/shadow still apply), or `enabled = false` and `hyprctl
reload` to return every window to its tiled slot.

**Blur not changing** — Expected. Blur size is global (`blur:size`); the plugin
cannot scale blur radius per window (see the config note above). The cards only
show blur behind them if `decoration:blur:enabled` is on.

**Multi-monitor** — The depth stack is anchored to the focused monitor's active
workspace. Windows on other monitors or workspaces are left untouched; the stack
re-anchors when focus crosses monitors.
```
