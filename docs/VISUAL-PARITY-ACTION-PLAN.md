# Visual Parity Action Plan — focusZ

**Goal:** Achieve visual parity with `~/Downloads/deep_1-1_000.zip` (140 PNG frames)
**Date:** 2026-08-12
**Current parity:** ~60% (core depth effect works, visual polish missing)

---

## Phase 1: Quick Wins (No render-path changes) — 1-2 days

### 1.1 Tune Scale/Opacity Parameters
**Task:** Adjust config defaults to better match target falloff curve
**Files:** `src/globals.hpp` (defaults), `README.md` (docs)
**Target values:**
- `layer1Scale`: 0.70 → ? (compare against frames 034-045)
- `layer2Scale`: 0.50 → ? (compare against frames 056-080)
- `layer1Opacity`: 0.85 → ? (compare against frames)
- `layer2Opacity`: 0.70 → ? (compare against frames)

**Test:** Capture output, compare against target frames

### 1.2 Adjust Scatter Positioning
**Task:** Modify `card_peek_min`/`card_peek_max` to match target card placement
**Files:** `src/globals.hpp` (defaults), `src/DepthFocus.cpp` (scatter logic)
**Current:** `peekMin=24`, `peekMax=80`
**Target:** Cards scattered across workarea, not just edges

**Test:** Visual comparison against frames 034-045

### 1.3 Verify Global Blur Settings
**Task:** Ensure `decoration:blur:enabled` is on with appropriate settings
**Files:** `~/.config/hypr/modules/focusz.lua` (blur config)
**Current:** `blur:size=30`, `blur:passes=8`, `blur:noise=0.0`
**Target:** Strong, clean blur behind back cards

**Test:** Visual comparison against frames 056-080

---

## Phase 2: Render-Path-Safe Features — 3-5 days

### 2.1 Card Borders via Decoration (M4.4)
**Task:** Implement border drawing in `CDepthShadowDecoration`
**Files:** `src/DepthShadow.cpp`, `src/DepthShadow.hpp`
**Approach:**
1. Re-enable `draw()` method (currently no-op)
2. Implement `drawShadow()` to render border rectangles
3. Use `CRegion` clipping to clip borders against front card
4. Add border width/color config to `FocusZConfig`

**Config keys:**
```cpp
card_border = true           // enable/disable
card_border_width = 2        // px (0-10)
card_border_color = 0x80ffffff  // 0xAARRGGBB
```

**Test:** Visual comparison against target frames, crash testing on v0.56.2

### 2.2 Frosted Glass Overlay (M4.2)
**Task:** Render semi-transparent overlay on back cards
**Files:** `src/DepthShadow.cpp`, `src/DepthShadow.hpp`
**Approach:**
1. Add frost rendering to `drawShadow()`
2. Render semi-transparent white/black rectangle over back cards
3. Adjust frost intensity based on depth layer
4. Use `CRegion` to clip frost to card bounds

**Config keys:**
```cpp
card_frost = true            // enable/disable
card_frost_strength = 0.4   // 0.0-1.0
```

**Test:** Visual comparison, performance testing (overlay rendering cost)

### 2.3 Depth Shadows (M4.5)
**Task:** Re-enable `drawShadow()` with crash testing
**Files:** `src/DepthShadow.cpp`
**Approach:**
1. Test `g_pHyprRenderer->drawShadow()` on v0.56.2
2. If crash-free, implement depth-aware shadows
3. If crash-prone, use Ryoku's native `decoration.shadow` (range 45)

**Test:** Crash testing on v0.56.2, visual comparison

---

## Phase 3: Advanced Features — 5-7 days

### 3.1 Canvas Plate (M4.3)
**Task:** Implement frosted matte surface under cards
**Files:** New `src/CanvasPlate.cpp`, `src/CanvasPlate.hpp`
**Approach:**
1. Create `CCanvasPlateDecoration` subclass of `IHyprWindowDecoration`
2. Render frosted rectangle behind deck
3. Apply blur to canvas plate independently
4. Adjust alpha based on deck fullness

**Config keys:**
```cpp
canvas_plate = true              // enable/disable
canvas_plate_alpha = 0.15        // 0.0-1.0
canvas_plate_alpha_max = 0.30    // 0.0-1.0
canvas_plate_frost = 0.55        // 0.0-1.0
```

**Test:** Visual comparison, crash testing

### 3.2 Wallpaper Canvas (M4.1)
**Task:** Capture and render dimmed/zoomed wallpaper
**Files:** New `src/WallpaperCanvas.cpp`, `src/WallpaperCanvas.hpp`
**Approach:**
1. Capture wallpaper as texture via Hyprland API
2. Render dimmed/zoomed version behind deck
3. Adjust zoom/dim based on deck fullness
4. Use `addPassElement` for background rendering

**Config keys:**
```cpp
wallpaper_dim = 0.5             // 0.0-1.0
wallpaper_zoom = 0.88           // 0.1-1.0
canvas_zoom_floor = 0.55        // 0.1-1.0
canvas_dim_floor = 0.15         // 0.0-1.0
canvas_shadow_boost = 1.5       // 0.0-5.0
```

**Test:** Visual comparison, crash testing, performance testing

### 3.3 Animation Easing
**Task:** Implement bezier easing for depth transitions
**Files:** `src/DepthFocus.cpp`
**Approach:**
1. Add `animation_bezier` config key
2. Implement bezier curve evaluation in `applyDepthToWindow()`
3. Use Hyprland's animation timing if available
4. Add `animation_duration` config key

**Config keys:**
```cpp
animation_bezier = "0.25, 0.1, 0.25, 1.0"  // cubic-bezier
animation_duration = 0.3                      // seconds
```

**Test:** Visual comparison, performance testing

---

## Phase 4: Integration & Polish — 2-3 days

### 4.1 Config Validation
**Task:** Ensure all new config keys have proper validation
**Files:** `src/main.cpp` (config registration)
**Approach:**
1. Add `SFloatValueOptions` for all new float configs
2. Add `SIntValueOptions` for all new int configs
3. Add `SBoolValueOptions` for all new bool configs
4. Test config parsing with invalid values

### 4.2 Documentation Update
**Task:** Update README.md with new features and config
**Files:** `README.md`
**Approach:**
1. Move reserved features to "Implemented" section
2. Add usage examples for new features
3. Update troubleshooting section

### 4.3 Release Preparation
**Task:** Prepare for v0.2.0 release
**Files:** `.github/workflows/release.yml`
**Approach:**
1. Update version in `CMakeLists.txt`
2. Update release notes
3. Test release workflow

---

## Testing Strategy

### Visual Testing
1. **Frame capture** — Capture plugin output at each depth state
2. **Side-by-side comparison** — Compare against target frames
3. **Parameter sweep** — Test each config value against target
4. **Multi-monitor** — Verify depth effect across monitors

### Crash Testing
1. **Render-path features** — Test each new feature for crashes on v0.56.2
2. **Plugin unload** — Verify clean teardown with all features enabled
3. **Rapid window operations** — Test open/close/focus changes during animation
4. **Multi-workspace** — Test switching workspaces with depth active

### Performance Testing
1. **Frame rate** — Measure FPS with 2, 4, 8, 16 windows in stack
2. **Memory** — Track memory usage with render-path features enabled
3. **CPU** — Profile hot paths with new features
4. **Render time** — Measure per-frame render cost

---

## Risk Assessment

### High Risk
- **Render-path crashes** — Previous attempts crashed on v0.56.2
- **Performance regression** — New rendering features may impact FPS

### Medium Risk
- **Config complexity** — Too many config keys may confuse users
- **Multi-monitor issues** — Depth effect may not work correctly across monitors

### Low Risk
- **Documentation drift** — README may become outdated
- **Release workflow** — New features may break CI/CD

---

## Success Criteria

### Visual Parity
- [ ] Card borders match target (2px white/light)
- [ ] Frosted glass effect matches target (strong blur behind back cards)
- [ ] Canvas plate matches target (frosted matte surface)
- [ ] Wallpaper canvas matches target (dimmed/zoomed background)
- [ ] Depth shadows match target (subtle per-card shadows)
- [ ] Animation matches target (smooth easing)

### Technical
- [ ] No crashes on Hyprland v0.56.2
- [ ] FPS stays above 60 with 8 windows in stack
- [ ] Memory usage stays under 10MB
- [ ] All config keys validated
- [ ] Documentation updated
- [ ] Release workflow passes

---

## Timeline

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1: Quick Wins | 1-2 days | None |
| Phase 2: Render-Path-Safe | 3-5 days | Phase 1 |
| Phase 3: Advanced | 5-7 days | Phase 2 |
| Phase 4: Integration | 2-3 days | Phase 3 |
| **Total** | **11-17 days** | |

---

## Next Steps

1. **Start Phase 1** — Tune scale/opacity parameters
2. **Capture baseline** — Screenshot current plugin output for comparison
3. **Begin Phase 2** — Implement card borders via decoration
4. **Test iteratively** — Compare against target after each feature
