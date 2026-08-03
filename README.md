# focusZ — Z-Axis Depth Focus Layout for Hyprland

A Hyprland plugin that creates a **3D-like stacking layout** based on Z-axis depth.
The focused window stays in the foreground at full scale, while background windows
recede visually with reduced scale, opacity, and dynamic shadows.

## Effect

| Layer | Position | Scale | Opacity | Blur | Shadow |
|-------|----------|-------|---------|------|--------|
| 0 (focused) | Foreground | 1.00 | 1.0 | No | Max |
| -1 | Background | 0.85 | 0.7 | Global* | Medium |
| -2 | Background | 0.70 | 0.4 | Global* | Min |

\* Blur is applied by Hyprland's global `blur:size` setting. The plugin API has no
per-window blur-radius control (only an on/off `noblur` window rule), so the
`layer_1_blur` / `layer_2_blur` toggles are reserved but not yet wired to
anything. Background windows blur exactly as much as your global `blur:size`.

Switching focus triggers a smooth interpolation animation that swaps the positions
in the Z-matrix.

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

        # How many windows participate in the depth stack (1-5)
        max_layers = 3

        # Scale factors per background layer
        layer_1_scale = 0.85      # 0.5 - 1.0
        layer_2_scale = 0.70      # 0.3 - 1.0

        # Opacity per background layer
        layer_1_opacity = 0.7     # 0.1 - 1.0
        layer_2_opacity = 0.4     # 0.1 - 1.0

        # Whether to apply blur to background layers
        # NOTE: reserved — per-window blur radius isn't part of the plugin API;
        # blur follows the global blur:size. These toggles currently do nothing.
        layer_1_blur = true
        layer_2_blur = true

        # Animation speed for depth transitions (higher = snappier)
        animation_speed = 8.0     # 1.0 - 20.0

        # Scale windows toward monitor center (true) or top-left (false)
        center_scale = true
    }
}
```

### Keybind

Cycle focus through the depth stack:

```hyprlang
bind = ALT, Tab, focusZ:cycle
```

## How It Works

The plugin hooks into three core Hyprland systems:

1. **EventBus listeners** — `window.active`, `window.open`, `window.close`,
   `window.destroy` events maintain an ordered Z-stack of windows. On focus change,
   the newly focused window is promoted to Layer 0 and all others demoted. The
   stack is anchored to the focused window's monitor, so windows on other monitors
   are never scaled or reordered.

2. **Per-window animated transforms** — Background windows get their
   `WINDOW_ALPHA_ACTIVE` animation variable goal set to the layer's opacity, and
   their `sizeAnimation`/`positionAnimation` goals updated to produce the scaled,
   centered effect. Hyprland's native animation system handles the smooth
   interpolation. The plugin records each window's original unscaled geometry and
   restores it when the window leaves the stack (evicted past `max_layers`,
   focused, disabled, or closed), so scaling never compounds.

3. **Render-stage hook** — A `RENDER_PRE_WINDOW` listener injects a
   `RMOD_TYPE_SCALECENTER` modification into the render data for each background
   window, applying the per-layer scale factor during the actual draw. Depth
   lookup here is O(1) (cached per window), and windows whose layer didn't change
   are not re-damaged.

4. **Custom shadow decoration** — Background windows receive a
   `CDepthShadowDecoration` (`IHyprWindowDecoration` subclass) that draws a shadow
   whose range/offset scales with the window's layer depth. The decoration is
   removed again when the window returns to Layer 0.

## Architecture

```
src/main.cpp            Plugin entry: PLUGIN_API_VERSION / PLUGIN_INIT / PLUGIN_EXIT
                    Registers config values, EventBus listeners, dispatcher
                    Version-hash check against running Hyprland

src/DepthFocus.hpp/cpp  CDepthFocusManager — the core depth engine
                    Maintains Z-stack, computes per-layer transforms,
                    applies animations, render-stage hook

src/DepthShadow.hpp/cpp CDepthShadowDecoration — IHyprWindowDecoration subclass
                    Draws depth-aware shadows (range/offset vary by layer)

src/globals.hpp         Plugin handle + config value smart pointers
```

## Troubleshooting

**"Version mismatch" notification on load** — The plugin must be compiled against
the exact same Hyprland headers as the running binary. Rebuild after any Hyprland
update: `make clean && make`.

**No visual effect** — Check `hyprctl getoption plugin:focusZ:enabled` and ensure
your windows aren't floating-only (the plugin works on both tiled and floating
windows but needs at least 2 windows on a workspace to show depth).

**Window position looks off after disabling** — Run `hyprctl reload` to let
Hyprland's layout recalculate window positions.

**Multi-monitor** — The depth stack is anchored to the focused monitor. Windows on
other monitors are left untouched; the stack re-anchors when focus crosses monitors.

**Blur not changing** — Expected. Blur size is global (`blur:size`); the plugin
cannot scale blur radius per window (see the config note above).
