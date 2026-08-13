# Visual Parity Report — focusZ vs Target Frames

**Date:** 2026-08-12
**Target:** `~/Downloads/deep_1-1_000.zip` — 140 PNG frames
**Plugin:** `libfocusZ.so` (compiled 2026-08-12)

---

## Executive Summary

The current plugin achieves **~60% visual parity** with the target frames. The core depth effect (scale + opacity + positioning) works correctly, but the visual polish that makes the target look refined is missing. Key gaps: card borders, frosted glass effect, and canvas plate are all reserved features that need render-path-safe implementation.

---

## Target Frame Analysis

### Frame Progression

| Frames | State | Visual Characteristics |
|--------|-------|------------------------|
| 000–032 | Flat (pre-depth) | Single terminal, gray background, no depth |
| 033–045 | Transition | Two windows visible, depth effect activating |
| 046–055 | Active depth | Multiple cards, strong scale falloff |
| 056–108 | Deep focus | 3+ cards, blurred back cards, clear hierarchy |
| 109–118 | Active depth | Various configurations |
| 119–139 | Return to flat | Back to single terminal |

### Key Visual Characteristics (Target)

1. **Card appearance:** Rounded corners, subtle white/light borders (~2px), semi-transparent
2. **Blur:** Strong blur behind back cards (frosted glass effect)
3. **Scale:** Back cards significantly smaller (strong depth falloff)
4. **Positioning:** Cards scattered across the workarea (not just edges)
5. **Background:** Flat gray (no wallpaper canvas visible in these frames)
6. **Opacity:** Back cards noticeably more transparent than front
7. **Shadows:** Subtle drop shadows under cards

---

## Current Plugin Behavior

### What Works (Visual Parity: ~60%)

| Feature | Status | Notes |
|---------|--------|-------|
| Card scaling | ✅ | Back cards get smaller boxes via `getTransformForLayer()` |
| Opacity reduction | ✅ | Per-layer opacity via `alpha()` |
| Card positioning | ✅ | Edge scatter or around-focused scatter |
| Focus promotion | ✅ | Clicking a card promotes it to Layer 0 |
| Stack rebuilding | ✅ | Identity check prevents unnecessary rebuilds |
| Workspace isolation | ✅ | Only anchor monitor's active workspace joins stack |
| Fullscreen skip | ✅ | Fullscreen windows not affected |

### What's Missing (Visual Parity: ~40%)

| Feature | Status | Config Key | Priority |
|---------|--------|------------|----------|
| Card borders | ❌ Reserved | `card_border`, `card_border_width`, `card_border_color` | High |
| Frosted glass | ❌ Reserved | `card_frost`, `card_frost_strength` | High |
| Canvas plate | ❌ Reserved | `canvas_plate`, `canvas_plate_alpha`, `canvas_plate_frost` | Medium |
| Wallpaper canvas | ❌ Reserved | `wallpaper_dim`, `wallpaper_zoom`, `canvas_zoom_floor` | Low |
| Depth shadow | ❌ Disabled | (decoration draw is no-op) | Medium |
| Animation easing | ❌ Reserved | `animation_speed` (only linear) | Low |

---

## Detailed Gap Analysis

### 1. Card Borders (M4.4) — HIGH PRIORITY

**Target:** Subtle white/light border (~2px) around every deck card, clipped against the front card so back-card rims never cross the focused window.

**Current:** No borders rendered. Config keys registered but inert.

**Implementation challenge:** Requires render-path code (drawing border rectangles). Previous attempt crashed via `addPassElement` on v0.56.2.

**Possible approach:**
- Use `IHyprWindowDecoration` subclass (like `CDepthShadowDecoration`) to draw borders
- Attach decoration to each deck card
- Clip border rendering against front card bounds
- Test for crash safety on v0.56.2

### 2. Frosted Glass (M4.2) — HIGH PRIORITY

**Target:** Translucent veil over back cards, denser with depth, creating a Z-axis frost effect.

**Current:** No frost effect. Back cards rely on global `decoration:blur:enabled`.

**Implementation challenge:** Requires per-window blur control or overlay rendering.

**Possible approach:**
- Render a semi-transparent white/black overlay on back cards
- Use `CRegion` clipping to apply frost only to back card areas
- Adjust frost intensity based on depth layer
- Alternative: Use global blur with per-window `noblur` rule (but this is all-or-nothing)

### 3. Canvas Plate (M4.3) — MEDIUM PRIORITY

**Target:** Frosted matte surface under the cards.

**Current:** No canvas plate. Cards float directly on workspace.

**Implementation challenge:** Requires background rendering layer.

**Possible approach:**
- Render a frosted rectangle behind the deck
- Use `addPassElement` with a custom render element
- Apply blur to the canvas plate independently of card blur

### 4. Wallpaper Canvas (M4.1) — LOW PRIORITY

**Target:** Dimmed/zoomed background layer behind the deck that recedes progressively.

**Current:** No wallpaper canvas. Background is workspace wallpaper.

**Implementation challenge:** Requires wallpaper capture and manipulation.

**Possible approach:**
- Capture wallpaper as texture
- Render dimmed/zoomed version behind deck
- Adjust zoom/dim based on deck fullness

### 5. Depth Shadow (M4.5) — MEDIUM PRIORITY

**Target:** Depth-aware shadows (range/offset vary by layer).

**Current:** `CDepthShadowDecoration` exists but `drawShadow()` is disabled (no-op).

**Implementation challenge:** `g_pHyprRenderer->drawShadow()` touches `m_renderData.pMonitor` which SEGVs on re-lease/modeset.

**Possible approach:**
- Use Ryoku's native `decoration.shadow` (range 45) for basic shadows
- Implement custom shadow rendering via `IHyprWindowDecoration`
- Test for crash safety on v0.56.2

### 6. Animation Easing — LOW PRIORITY

**Target:** Smooth bezier easing for depth transitions.

**Current:** `animation_speed` config exists but only affects linear interpolation.

**Implementation challenge:** Requires animation timing system.

**Possible approach:**
- Implement bezier easing in `applyDepthToWindow()`
- Use Hyprland's animation system if available
- Add `animation_bezier` config key

---

## Visual Comparison Table

| Aspect | Target | Current | Gap |
|--------|--------|---------|-----|
| Card scale (Layer 1) | ~70% | 70% (config) | ✅ Match |
| Card scale (Layer 2) | ~50% | 50% (config) | ✅ Match |
| Card opacity (Layer 1) | ~85% | 85% (config) | ✅ Match |
| Card opacity (Layer 2) | ~70% | 70% (config) | ✅ Match |
| Card positioning | Scattered across workarea | Edge/around scatter | ⚠️ Partial |
| Card borders | 2px white/light | None | ❌ Missing |
| Blur behind cards | Strong frosted glass | Global blur only | ❌ Missing |
| Canvas plate | Frosted matte surface | None | ❌ Missing |
| Wallpaper canvas | Dimmed/zoomed background | None | ❌ Missing |
| Drop shadows | Subtle per-card | Disabled | ❌ Missing |
| Animation | Smooth easing | Linear only | ⚠️ Partial |

---

## Recommendations

### Phase 1: Quick Wins (No render-path changes)

1. **Tune existing parameters** — Adjust `layer1Scale`, `layer2Scale`, `layer1Opacity`, `layer2Opacity` to better match target falloff curve
2. **Adjust scatter positioning** — Modify `card_peek_min`/`card_peek_max` to match target card placement
3. **Enable global blur** — Ensure `decoration:blur:enabled` is on with appropriate `blur:size` (30) and `blur:passes` (8)

### Phase 2: Render-Path-Safe Features

4. **Card borders via decoration** — Implement border drawing in `CDepthShadowDecoration` (already has render hook)
5. **Frosted glass overlay** — Render semi-transparent overlay on back cards via `IHyprWindowDecoration`
6. **Depth shadows** — Re-enable `drawShadow()` with crash testing on v0.56.2

### Phase 3: Advanced Features

7. **Canvas plate** — Implement via `addPassElement` with crash testing
8. **Wallpaper canvas** — Capture and render dimmed/zoomed wallpaper
9. **Animation easing** — Implement bezier curves for smooth transitions

---

## Testing Plan

### Visual Testing

1. **Frame comparison** — Capture plugin output at each depth state and compare against target frames
2. **Parameter sweep** — Test each config value against target to find optimal settings
3. **Multi-monitor** — Verify depth effect works correctly across monitors

### Crash Testing

1. **Render-path features** — Test each new render-path feature for crashes on v0.56.2
2. **Plugin unload** — Verify clean teardown with all features enabled
3. **Rapid window operations** — Test open/close/focus changes during animation

### Performance Testing

1. **Frame rate** — Measure FPS with 2, 4, 8, 16 windows in stack
2. **Memory** — Track memory usage with render-path features enabled
3. **CPU** — Profile hot paths with new features

---

## Conclusion

The plugin achieves solid functional parity (core depth effect works), but visual parity requires implementing the reserved features (borders, frost, canvas). The main challenge is doing this safely on Hyprland v0.56.2 without reintroducing the crashes that led to the render-path-free architecture.

**Recommended next step:** Start with card borders via `IHyprWindowDecoration` (safest render-path approach), then test frosted glass overlay.
