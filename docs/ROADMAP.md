# focusZ Roadmap v2

**Repository:** `Lowingx/hypr-focuZ` · **Board:** [focusZ Kanban](https://github.com/users/Lowingx/projects/3)

> Live status lives on the kanban board. This document explains **why each
> milestone exists**, **what each task involves**, and **how to know a task is
> done** — enough context for a new contributor to pick up work cold.
>
> **Visual target:** screenshots in `~/Downloads/deep_1-1_000.zip`
> (`deep_1-1_*.png`). These 140 frames define the exact look (depth, blur,
> canvas). The PNGs show: dark wallpaper canvas behind the deck, frosted glass
> blur on back cards, strong depth scale falloff, card borders/rims, and a
> dark wallpaper scene with a hooded figure.

## 1. Vision

focusZ is a Hyprland plugin that renders windows in a Z-axis depth layout: the
focused window sits in the foreground at full scale while background windows
recede with reduced scale, opacity, and a depth-scaled shadow. The product goal
is **zero-friction depth-focus**: it must feel instant, must never corrupt
window geometry, and must never degrade compositor performance.

## 2. Load-bearing constraints (do not break these)

1. **Render-path-free.** The plugin must not touch Hyprland's render path —
   no `addPassElement`, no `IWindowTransformer`, no decoration `draw()`. The
   render path SEGVs on v0.56.2 (see `hyprlandCrashReport*.txt`). All visual
   effects come from safe layout + opacity + native blur only.
2. **Hot path is O(1).** The focus/open/close event handlers fire per window.
   Depth must come from `m_depthCache` (rebuilt only on stack mutations), and
   unchanged windows must not be re-damaged.
3. **Scaling never compounds.** `m_applied` (`SDepthState`) is the single
   source of truth for each window's depth, decoration, and original unscaled
   geometry. Restore reads that geometry back.
4. **Per-monitor isolation.** The depth stack is anchored to the focused
   window's monitor. Other monitors are never touched.
5. **ABI lock.** The plugin is ABI-locked to the exact Hyprland build it was
   compiled against (`PLUGIN_INIT` hash check). Any Hyprland upgrade requires
   rebuild + reload.
6. **Config name sync.** Options are registered in `src/main.cpp`
   (`addConfigValueV2`) and must match `hyprland.conf` under `plugin:focusZ:`
   exactly.
7. **No overview interference.** focusZ must never bind `ALT+Tab` or
   `SUPER+Tab` — those belong to Ryoku's overview. `focusZ:cycle` is only
   invoked via explicit config bind.

## 3. Definition of Done (applies to every task)

- `cmake -B build && cmake --build build` compiles with **zero errors**.
- Plugin loads live: `hyprctl plugin list` shows it, and
  `hyprctl getoption plugin:focusZ:enabled` responds.
- The relevant manual checklist passes (see M4.1).
- If the change is architectural, an ADR was written or updated.
- If config or behavior changed, README / KANBAN.md were updated.
- Config option names in `src/main.cpp` and `hyprland.conf` stay in sync.

## 4. Milestones

Status legend: `Done` · `In Progress` · `Todo` · `Blocked` · `Candidate`
Priority: **P0** = critical, **P1** = should, **P2** = nice-to-have.
Effort: **S** < 1h · **M** < 1d · **L** > 1d.

---

### M0 — Foundation (shipped)

**Goal:** the plugin builds, loads, and produces the depth effect on the
focused monitor.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M0.1 | Plugin skeleton: entry point, config registration, `focusZ:cycle` dispatcher | Done | P0 | M |
| M0.2 | Depth stack anchored to the focused monitor, re-anchors on focus crossing | Done | P0 | M |
| M0.3 | Window lifecycle: restore on evict / disable / close, no scale compounding | Done | P0 | M |
| M0.4 | O(1) render hook: depth cache + skip-unchanged, shadow decoration lifecycle | Done | P0 | M |

---

### M1 — Stability pivot (shipped)

**Goal:** stop crashing Hyprland. Remove all render-path code so the plugin
cannot SEGV the compositor.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M1.1 | Remove `CScaleTransformer` attachment (dead code in stacking mode) | Done | P0 | S |
| M1.2 | Empty `onRenderStage` — no `drawCardBorders`/`drawCanvas`/`drawCardFrost` | Done | P0 | S |
| M1.3 | Remove `CDepthShadowDecoration` attachment (no-op draw, was in render path) | Done | P0 | S |
| M1.4 | Deck via safe primitives: floating geometry + alpha + native blur | Done | P0 | M |

**Root cause of v1 crashes:** `drawCardBorders()` called
`g_pHyprRenderer->addPassElement()` inside `RENDER_POST_WINDOWS`, which
SEGVs inside Hyprland's `addPassElement` on v0.56.2 (backtrace frame #4).
The render path was the only source of crashes; layout + opacity are safe.

---

### M2 — Docs & records (shipped)

**Goal:** a new contributor can onboard in under 30 minutes.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M2.1 | ADR suite in `docs/adr/` covering the load-bearing decisions | In Progress | P0 | M |
| M2.2 | `CONTRIBUTING.md`: onboarding, verification loop, DoD, review checklist | Todo | P1 | S |
| M2.3 | Issue & PR templates + label taxonomy (`priority:*`, `area:*`, `status:*`) | Todo | P1 | S |
| M2.4 | README / KANBAN.md reconciled with current state | Done | P1 | S |

---

### M3 — Quality gates

**Goal:** no regression ships silently.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M3.1 | Formalize the manual verification checklist (from AGENTS.md) into a runnable script + doc | Todo | P1 | M |
| M3.2 | CI build verification: GitHub Actions compiling against pinned Hyprland headers | Done | P1 | M |
| M3.3 | ADR-compliance review gate on PRs | Candidate | P2 | S |

---

### M4 — Visual parity (v2)

**Goal:** match the visual target (deep_1-1_*.png). Triage here against the
constraint in §2 — each feature must stay render-path-free.

| ID  | Task | Status | Prio | Effort | Blocked by |
|-----|------|--------|------|--------|------------|
| M4.1 | Wallpaper canvas: dimmed/zoomed background layer behind the deck | Todo | P1 | L | render-path-free re-add |
| M4.2 | Blur behind back cards: native blur + per-card frost via decoration | Todo | P1 | L | render-path-free re-add |
| M4.3 | Z-depth perception: steeper scale/opacity falloff tuned to target PNGs | Todo | P1 | M | — |
| M4.4 | Card borders: rim around back cards, clipped against front | Todo | P2 | L | render-path-free re-add |
| M4.5 | Layer exclusions: per-class rules to never depth (docks, floats) | Todo | P1 | M | — |
| M4.6 | Dim on startup: deck renders immediately without click-to-focus | Todo | P1 | M | — |

**M4.1, M4.2, M4.4 are blocked** by the architectural decision to stay
render-path-free. Each requires a new safe render mechanism or integration
with Hyprland's native features. Revisit when the render-path-free constraint
is revisited or a safe API surface becomes available.

**M4.3** is unblocked — the scale/opacity falloff can be tuned via config
values (`layer_1_scale`, `layer_2_scale`, `layer_1_opacity`, `layer_2_opacity`)
without touching the render path.

---

### M5 — Config & UX

**Goal:** make the plugin feel polished and configurable.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M5.1 | Animation easing/bezier per transition | Todo | P2 | S |
| M5.2 | Shadow intensity/color config for background layers | Todo | P2 | S |
| M5.3 | Multi-monitor participation policy | Todo | P2 | M |
| M5.4 | Configurable depth→transform curves (scale/opacity as f(layer)) | Todo | P2 | M |

---

### M6 — Ryoku integration

**Goal:** ship focusZ inside the Ryoku distro.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M6.1 | `ryoku-focusZ` PKGBUILD (release/packages/) | Todo | P0 | M |
| M6.2 | Module sync: `hyprland/modules/focusz.lua` tracks plugin version | Todo | P1 | M |
| M6.3 | ABI rebuild flow: detect `hyprland` package change, trigger rebuild | Todo | P1 | M |
| M6.4 | Defaults in `hyprland.lua`: when module is on, config values | Todo | P1 | S |
| M6.5 | Integration roadmap for neuromap (standalone doc) | Done | P0 | M |

---

## 5. Suggested timeline

```
Now ─────────────────────────────────────────────────────────────►
[M2] finish ADRs ─► onboarding docs ─► templates      (M2.1 → M2.4)
[M3] checklist ───► CI build gate                       (M3.1 → M3.2)
[M4] M4.3 (depth tuning) first; M4.5/M4.6 next         (unblocked items)
[M5] triaged continuously; start with M5.1              (ongoing)
[M6] M6.5 done; M6.1 (PKGBUILD) when neuromap reviews  (ongoing)
```

- **Next up:** M2.1 (finish ADRs) → M2.2/M2.3 → M3.1.
- **High-value early win:** M4.3 (depth tuning) is unblocked and improves
  visual parity immediately with config-only changes.
- **M4.1, M4.2, M4.4 (canvas, frost, borders)** require revisiting the
  render-path-free constraint or finding a safe alternative. The dead code in
  `drawCanvas()`, `drawCardFrost()`, and `drawCardBorders()` is kept for
  reference but must not be re-enabled without a safe render mechanism.

## 6. How to work on this project

1. Pick an issue from the kanban board → move to **In Progress** (assign
   yourself).
2. Branch: `feat/<issue>-<slug>`.
3. Implement against the invariants in §2 and the DoD in §3.
4. Verify: build clean → live load → manual checklist.
5. Open a PR → move to **In Review** → merge → move to **Done** → close the
   issue once docs reflect the state.

## 7. Label taxonomy

| Label | Meaning |
|-------|---------|
| `priority:p0` / `p1` / `p2` | Critical / should / nice-to-have |
| `area:core` | Depth engine, layout, geometry |
| `area:docs` | README, AGENTS.md, ADRs, CONTRIBUTING |
| `area:ci` | Build verification, automation |
| `area:ops` | Upgrade flow, releases, support |
| `area:feature` | Product backlog items |
| `area:visual` | Blur, canvas, borders, depth tuning |
| `status:blocked` | Waiting on API or upstream |
| `status:good-first-issue` | Onboarding friendly |
| `status:help-wanted` | Open for external contribution |
