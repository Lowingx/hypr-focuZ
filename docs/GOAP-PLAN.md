# focusZ — SPARC-GOAP Implementation Plan

**Goal:** Reach visual parity with the depth-focus target (140 PNGs)  
**Current State:** Stable plugin with safe primitives (opacity + floating geometry)  
**Target State:** Full visual effect: canvas, borders, frost, depth tuning

---

## 1. State Analysis

### Current State (M0-M2 shipped)
```yaml
core:
  stacking: working          # floating cards positioned correctly
  scale_per_layer: working   # getTransformForLayer() interpolates
  opacity_per_layer: working # alphaVar per window
  card_scatter: working      # edge + around modes
  focus_promote: working     # promoteWindow() reorders stack
  per_monitor: working       # anchored to focused monitor
  
render:
  onRenderStage: NO-OP       # disabled (SEGV root cause)
  drawCardBorders: DEAD      # empty body
  drawCanvas: DEAD           # empty body
  drawCardFrost: DEAD        # empty body
  scaleTransformer: DEAD     # never attached
  
config:
  registered: 27 keys
  active: 12 keys (enabled, stacking, maxLayers, layer1/2Scale, layer1/2Opacity, animationSpeed, frontScale, edgeScatter, scatterReshuffle, peekMin/Max, centerScale, debug)
  inert: 15 keys (wallpaperDim, wallpaperZoom, canvas*, cardBorder, borderWidth, borderColor, cardFrost, cardFrostStrength, plateFrost, canvasPlate)
  
constraints:
  render_path_free: MANDATORY  # no addPassElement, no IWindowTransformer
  abi_lock: HARD               # __hyprland_api_get_hash()
  no_overview: HARD            # never touch ALT+Tab/SUPER+Tab
```

### Goal State (visual parity)
```yaml
visual:
  canvas: dimmed/zoomed wallpaper behind deck
  borders: rim around every card, clipped against front
  frost: frosted-glass veil over back cards, depth-scaled
  depth_curve: matches PNG target within 5%
  dim_on_startup: deck renders without click-to-focus
  
config:
  all_27_keys: ACTIVE
  defaults: tuned to visual target
  
quality:
  build: zero errors
  crash: zero SEGV
  performance: O(1) hot path maintained
```

---

## 2. Milestone Plan (SPARC-enhanced)

### Phase 1: Specification (Issues #16-#18)

**Goal:** Define the render-path-safe mechanism for visual features

| Milestone | SPARC Phase | Issue | Deliverables | Success Criteria | Est. Hours |
|-----------|-------------|-------|--------------|------------------|------------|
| M4.1 Canvas Spec | Specification | #17 | API design doc, render hook plan | Safe mechanism identified | 4h |
| M4.2 Frost Spec | Specification | #18 | Frost overlay design, blur integration | Blur API surface mapped | 3h |
| M4.4 Borders Spec | Specification | #16 | Border decoration strategy, clipping plan | No addPassElement needed | 3h |

**Preconditions:** None (unblocked)  
**Dependencies:** None (parallel work)

---

### Phase 2: Pseudocode (Design algorithms)

**Goal:** Plan the action sequences and state transitions

| Milestone | SPARC Phase | Deliverables | Success Criteria | Est. Hours |
|-----------|-------------|--------------|------------------|------------|
| Canvas Algorithm | Pseudocode | Dim/zoom logic, progressive recession | State machine validated | 4h |
| Frost Algorithm | Pseudocode | Depth-scaled opacity, clipping | Edge cases covered | 3h |
| Borders Algorithm | Pseudocode | Clipping against front card, rim segments | Algorithm pseudocode complete | 4h |

**Preconditions:** Phase 1 complete  
**Dependencies:** None (parallel work)

---

### Phase 3: Architecture (Structure the solution)

**Goal:** Design system components and integration points

| Milestone | SPARC Phase | Deliverables | Success Criteria | Est. Hours |
|-----------|-------------|--------------|------------------|------------|
| Canvas Architecture | Architecture | Component diagram, data flow | Integration points defined | 3h |
| Frost Architecture | Architecture | Decoration lifecycle, blur sampling | Interface contracts clear | 3h |
| Borders Architecture | Architecture | Clipping strategy, render order | No render-path violations | 3h |

**Preconditions:** Phase 2 complete  
**Dependencies:** None (parallel work)

---

### Phase 4: Refinement (TDD implementation)

**Goal:** Implement with test-driven development

| Milestone | SPARC Phase | Issue | Deliverables | Success Criteria | Est. Hours |
|-----------|-------------|-------|--------------|------------------|------------|
| M4.6 Dim-on-startup | Refinement | #20 | Startup hook, initial rebuild | Deck renders without click | 4h |
| M4.5 Layer exclusions | Refinement | #21 | Exclusion config, filter logic | Docks never depth | 3h |
| M4.3 Depth tuning | Refinement | #19 | Config defaults, PNG analysis | Curve matches target ±5% | 6h |

**Preconditions:** Phase 3 complete  
**Dependencies:** M4.3 depends on PNG analysis

---

### Phase 5: Completion (Integration + validation)

**Goal:** Achieve visual parity and ship

| Milestone | SPARC Phase | Issue | Deliverables | Success Criteria | Est. Hours |
|-----------|-------------|-------|--------------|------------------|------------|
| Canvas Implementation | Completion | #17 | Working canvas, dimmed wallpaper | Visual matches PNG | 8h |
| Frost Implementation | Completion | #18 | Frosted-glass overlay | Depth-scaled frost visible | 6h |
| Borders Implementation | Completion | #16 | Card rims, clipping | No rim crosses front card | 8h |
| Config Sync | Completion | #27-#30 | PKGBUILD, module, ABI flow | Ryoku package ready | 6h |

**Preconditions:** Phase 4 complete  
**Dependencies:** Canvas → Frost → Borders (sequential, visual layering)

---

## 3. Action Graph (GOAP)

```
START
  │
  ├─► [M4.1 Canvas Spec] ──► [Canvas Algorithm] ──► [Canvas Arch] ──► [Canvas Impl]
  │                                                                 │
  ├─► [M4.2 Frost Spec] ──► [Frost Algorithm] ──► [Frost Arch] ───► [Frost Impl]
  │                                                                │
  ├─► [M4.4 Borders Spec] ──► [Borders Algorithm] ──► [Borders Arch] ──► [Borders Impl]
  │                                                                    │
  ├─► [M4.6 Dim-on-startup] (independent)                              │
  │                                                                    │
  ├─► [M4.5 Layer exclusions] (independent)                            │
  │                                                                    │
  └─► [M4.3 Depth tuning] ──► [PNG analysis] ──► [Config update]      │
                                                                       │
                                                                       ▼
                                                                  [CONFIG SYNC]
                                                                       │
                                                                       ▼
                                                                     DONE
```

---

## 4. Risk Assessment

| Risk | Type | Mitigation | Impact |
|------|------|------------|--------|
| Render-path-free violation | Technical | Strict code review, no addPassElement | HIGH |
| ABI lock on Hyprland upgrade | Operational | Document rebuild flow, consider pacman hook | MEDIUM |
| Blur API limitation | Technical | Accept global blur, document limitation | LOW |
| PNG analysis accuracy | Quality | Use mimo image analysis, validate with live testing | MEDIUM |
| Scope creep | Timeline | Stick to M4 visual parity, defer M5/M6 | MEDIUM |

---

## 5. Success Metrics

### Visual Parity Metrics
- Canvas dim: matches PNG within 5% opacity tolerance
- Canvas zoom: matches PNG within 3% scale tolerance
- Frost opacity: depth-scaled correctly (0.7x at depth 1, 0.95x at depth 2)
- Border visibility: rim visible on all cards, clipped correctly
- Depth curve: scale/opacity matches PNG target

### Code Quality Metrics
- Build: zero errors
- Crash: zero SEGV (render-path-free maintained)
- Performance: O(1) hot path (no regression)
- Config: all 27 keys active and documented

### Delivery Metrics
- GitHub issues: all M4 issues closed
- Documentation: README updated with visual features
- Package: PKGBUILD ready for Ryoku

---

## 6. Execution Timeline

```
Week 1: Specification + Pseudocode (Phases 1-2)
  ├─ Day 1-2: Canvas/Frost/Borders spec (parallel)
  └─ Day 3-5: Algorithm design (parallel)

Week 2: Architecture + Refinement (Phases 3-4)
  ├─ Day 1-2: Architecture design (parallel)
  ├─ Day 3-4: Dim-on-startup + Layer exclusions
  └─ Day 5: Depth tuning (PNG analysis)

Week 3: Completion (Phase 5)
  ├─ Day 1-3: Canvas implementation
  ├─ Day 4-5: Frost implementation
  └─ Day 6-7: Borders implementation

Week 4: Integration + Ship
  ├─ Day 1-2: Config sync, PKGBUILD
  ├─ Day 3: Live testing, visual validation
  └─ Day 4: Documentation, release
```

**Total Estimated Hours:** 80h  
**Parallel Efficiency:** ~40% (phases 1-3 can run in parallel)  
**Critical Path:** Canvas → Frost → Borders (sequential visual layering)

---

## 7. Preconditions Checklist

Before starting Phase 1:
- [ ] Visual target PNGs analyzed (140 frames)
- [ ] Render-path-free constraint documented
- [ ] Config keys mapped (active vs inert)
- [ ] GitHub board updated (issues #16-#30)

Before starting Phase 4:
- [ ] Architecture decisions recorded (ADRs)
- [ ] Test strategy defined
- [ ] Live testing environment ready

Before starting Phase 5:
- [ ] All Phase 4 deliverables complete
- [ ] Visual validation framework ready
- [ ] Ryoku integration plan reviewed

---

## 8. Definition of Done

The goal is achieved when:
1. All 27 config keys are active and documented
2. Canvas, frost, and borders render correctly
3. Visual parity with PNG target (within tolerances)
4. Zero crashes (render-path-free maintained)
5. Build passes with zero errors
6. GitHub issues #16-#30 closed
7. README updated with visual features
8. PKGBUILD ready for Ryoku package
