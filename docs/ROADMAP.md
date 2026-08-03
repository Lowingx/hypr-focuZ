# focusZ Roadmap

**Repository:** `Lowingx/hypr-focuZ` · **Board:** [focusZ Kanban](https://github.com/users/Lowingx/projects/3)

> Live status lives on the kanban board. This document explains **why each
> milestone exists**, **what each task involves**, and **how to know a task is
> done** — enough context for a new contributor to pick up work cold.

## 1. Vision

focusZ is a Hyprland plugin that renders windows in a Z-axis depth layout: the
focused window sits in the foreground at full scale while background windows
recede with reduced scale, opacity, and a depth-scaled shadow. Switching focus
animates the Z-stack smoothly using Hyprland's native animation system.

The product goal is **zero-friction depth-focus**: it must feel instant, must
never corrupt window geometry, and must never degrade compositor performance.

## 2. Load-bearing constraints (do not break these)

Every task below exists to protect or extend these invariants. A change that
violates one is not ready to merge regardless of how it's tested.

1. **Hot path is O(1).** The `RENDER_PRE_WINDOW` hook fires per window per
   frame. Depth must come from `m_depthCache` (rebuilt only on stack
   mutations), and unchanged windows must not be re-damaged.
2. **Scaling never compounds.** `m_applied` (`SAppliedState`) is the single
   source of truth for each window's depth, decoration, and original unscaled
   geometry. Restore reads that geometry back; it is captured once and
   re-captured only when the layout goal grows past `orig * scale`.
3. **Per-monitor isolation.** The depth stack is anchored to the focused
   window's monitor. Other monitors are never touched.
4. **ABI lock.** The plugin is ABI-locked to the exact Hyprland build it was
   compiled against (`PLUGIN_INIT` hash check). Any Hyprland upgrade requires
   `make clean && make` + reload.
5. **Config name sync.** Options are registered in `src/main.cpp`
   (`makeConfigValue`) and must match `hyprland.conf` under `plugin:focusZ:`
   exactly.
6. **No headless tests.** A Hyprland plugin cannot be tested headless. The
   only verification is build + load in a live session (manual loop in
   AGENTS.md).

## 3. Definition of Done (applies to every task)

- `make clean && make` compiles with **zero warnings** (`-Wall -Wextra`).
- Plugin loads live: `hyprctl plugin list` shows it, and
  `hyprctl getoption plugin:focusZ:enabled` responds.
- The relevant manual checklist passes (see M2.1).
- If the change is architectural, an ADR was written or updated.
- If config or behavior changed, README / AGENTS.md / KANBAN.md were updated.
- Config option names in `src/main.cpp` and `hyprland.conf` stay in sync.

## 4. Milestones

Status legend: `Done` · `In Progress` · `Todo` · `Blocked` · `Candidate`
Priority: **P0** = critical, **P1** = should, **P2** = nice-to-have.
Effort: **S** < 1h · **M** < 1d · **L** > 1d.

---

### M0 — Foundation: core depth engine

**Goal:** the plugin builds, loads, and produces the depth effect on the
focused monitor. *Shipped in the working tree; not yet committed.*

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M0.1 | Plugin skeleton: entry point, config registration, `focusZ:cycle` dispatcher | Done | P0 | M |
| M0.2 | Depth stack anchored to the focused monitor, re-anchors on focus crossing | Done | P0 | M |
| M0.3 | Window lifecycle: restore on evict / disable / close, no scale compounding | Done | P0 | M |
| M0.4 | O(1) render hook: depth cache + skip-unchanged, shadow decoration lifecycle | Done | P0 | M |

**Ship blocker:** nothing technical remains — the working tree must be
committed (see `git status`). That commit closes #1, #2, #3.

---

### M1 — Collaborative foundation: records, docs, tooling

**Goal:** a new contributor can onboard in under 30 minutes, and the project
has a paper trail that survives the rewrite. *This milestone makes the rest of
the roadmap safe to parallelize.*

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M1.1 | ADR suite in `docs/adr/` covering the load-bearing decisions | In Progress | P0 | M |
| M1.2 | `CONTRIBUTING.md`: onboarding, verification loop, DoD, review checklist | Todo | P1 | S |
| M1.3 | Issue & PR templates + label taxonomy (`priority:*`, `area:*`, `status:*`) | Todo | P1 | S |
| M1.4 | README / AGENTS.md / KANBAN.md reconciled with the ADRs | Todo | P1 | S |

**M1.1 scope** — one ADR per decision, each ~1 page:
- **ADR-001** Per-monitor depth-stack anchoring.
- **ADR-002** `SAppliedState` + original-geometry tracking (anti-compounding).
- **ADR-003** Shadow-decoration lifecycle and the blur API limitation.
- **ADR-004** ABI-lock policy: hash check, rebuild flow, upgrade procedure.
- **ADR-005** Config ownership: `src/main.cpp` registration ↔ `hyprland.conf`
  sync contract.

**Acceptance (M1.1):** `docs/adr/` exists with numbered entries; each record
states context / decision / consequences; ADRs referenced from README.

---

### M2 — Quality gates

**Goal:** no regression ships silently. Every change is verifiable without a
fresh Hyprland session on the author's machine.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M2.1 | Formalize the manual verification checklist (from AGENTS.md) into a runnable script + doc | Todo | P1 | M |
| M2.2 | CI build verification: GitHub Actions compiling `make` against pinned Hyprland headers | Todo | P1 | M |
| M2.3 | Extract pure transform math (`getTransformForLayer`, scale/center offset) for unit tests | Candidate | P2 | M |
| M2.4 | ADR-compliance review gate on PRs (ADRs must exist for architectural changes) | Candidate | P2 | S |

**M2.1 scope** — turn the prose loop into a checklist with pass/fail steps:
load (`hyprctl plugin list`), config read (`getoption`), focus cycle with ≥2
windows, eviction past `max_layers`, disable + `hyprctl reload` geometry
restore, multi-monitor anchor, close-while-stacked.

**M2.2 notes** — deps come via pkg-config: `hyprland`, `pixman-1`, `libdrm`.
Pin the `hyprland` version (currently 0.56.1) so CI matches the ABI contract.

---

### M3 — Operational hardening

**Goal:** the plugin survives Hyprland upgrades and real-world daily use
without manual archaeology.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M3.1 | Rebuild-on-upgrade flow: helper (`make upgrade` or hook) + documented procedure | Todo | P1 | M |
| M3.2 | Reproducible build: pin exact deps, parity between CI and dev machines | Candidate | P2 | M |
| M3.3 | Release process: tagged releases + changelog (from M0-M2 checklists) | Candidate | P2 | S |
| M3.4 | Compatibility/support matrix doc (Hyprland versions tested) | Candidate | P2 | S |

**M3.1 context** — the plugin self-unloads on version-hash mismatch. A script
that detects a `hyprland` package change and triggers rebuild + reload turns a
frequent footgun into a one-command operation.

---

### M4 — Feature backlog

**Goal:** grow the effect. Triage here happens against the constraints in §2 —
each feature must keep the hot path O(1) and the geometry restore exact.

| ID  | Task | Status | Prio | Effort |
|-----|------|--------|------|--------|
| M4.1 | Per-layer blur radius (#7) | Blocked | P2 | L |
| M4.2 | Layer exclusions: per-class rules to never depth (e.g. docks, floats) | Todo | P1 | M |
| M4.3 | Configurable depth→transform curves (scale/opacity as a function of layer) | Todo | P2 | M |
| M4.4 | Shadow intensity/color config for background layers | Todo | P2 | S |
| M4.5 | Animation easing/bezier per transition | Todo | P2 | S |
| M4.6 | Multi-monitor participation policy (which monitors participate) | Todo | P2 | M |

**M4.1 is blocked** by the plugin API: there is only an on/off `noblur` window
rule, and blur radius is the global `blur:size`. The `layer_1_blur` /
`layer_2_blur` options are registered for config compatibility but inert.
Revisit only if a render-pass blur hook becomes available; otherwise keep
documented as a limitation.

---

## 5. Suggested timeline

```
Now ─────────────────────────────────────────────────────────────►
[M1] finish ADRs ─► onboarding docs ─► templates      (M1.1 → M1.4)
[M2] checklist ───► CI build gate                       (M2.1 → M2.2)
[M3] upgrade flow ─► reproducible build ─► release      (M3.1 → M3.4)
[M4] triaged continuously; start with M4.2              (ongoing)
```

- **Next up (shortest path to "safe for contributors"):** M1.1 (finish ADR-004
  and ADR-005) → M1.2/M1.3 → M2.1 → M2.2.
- **High-value early win:** M4.2 (layer exclusions) is the most-requested
  production feature and is unblocked by any API.
- **Keep in mind:** nothing ships without §3 DoD and the §2 invariants.

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
| `area:core` | Depth engine, render hook, geometry |
| `area:docs` | README, AGENTS.md, ADRs, CONTRIBUTING |
| `area:ci` | Build verification, automation |
| `area:ops` | Upgrade flow, releases, support |
| `area:feature` | Product backlog items |
| `status:blocked` | Waiting on API or upstream |
| `status:good-first-issue` | Onboarding friendly |
| `status:help-wanted` | Open for external contribution |
