# focusZ Kanban v2

The **`Lowingx/hypr-focuZ`** team board — a Hyprland plugin implementing a
Z-axis depth-focus layout.

> **Operational view.** For *why* each task exists, its scope, and its
> definition of done, read the **[ROADMAP](ROADMAP.md)**. This board mirrors
> the roadmap milestones; labels map to the taxonomy defined there.

## Board

- **Platform:** GitHub Projects (v2)
- **Repository:** `Lowingx/hypr-focuZ`
- **Board name:** `focusZ Kanban`
- **Board URL:** https://github.com/users/Lowingx/projects/3
- **Status field:** `Todo` → `In Progress` → `In Review` → `Done`

## How the team uses this board

1. **Take work** — pick an open issue, assign yourself, move it to
   `In Progress`.
2. **Keep it moving** — update the card as the work progresses; comment when a
   decision is made so the reasoning is on the record.
3. **Hand off** — open a PR, move the card to `In Review`, and add a reviewer.
4. **Verify together** — a card reaches `Done` only when `make check` (the
   `-Werror` CI gate) passes, `./run-tests.sh` is green, and the change loads
   in a live Hyprland session (there is no headless test for the compositor
   side; see AGENTS.md). Close the issue once the repo docs reflect the state.
5. **Retrospect** — at milestone boundaries, reconcile the board against
   `ROADMAP.md` and archive stale cards.

Cards that look stuck get a `help-wanted` label. Anything that needs the
plugin API to grow first gets `status:blocked`.

## Current items

### Shipped (M0–M2)

| Issue | Title | Status | Prio | Area | Milestone |
|-------|-------|--------|------|------|-----------|
| #1 | Anchor the depth stack to the focused monitor | Done | P0 | core | M0 |
| #2 | Restore windows on evict / disable / close | Done | P0 | core | M0 |
| #3 | Keep the per-frame render hook O(1) | Done | P0 | core | M0 |
| #4 | Record architecture decisions as ADRs | Done | P0 | docs | M2 |
| #8 | Add CI build verification | Done | P1 | ci | M3 |
| #13 | Fix: deck mixed windows from different workspaces on the same monitor | Done | P0 | core | M0 |

### Visual Parity Analysis (Completed)

| Issue | Title | Status | Prio | Area | Milestone |
|-------|-------|--------|------|------|-----------|
| #31 | Visual parity analysis — target frame comparison | Done | P0 | docs | M4 |
| #32 | Test infrastructure — Google Test setup | Done | P1 | ci | M3 |
| #33 | Custom tester agent — Hyprland plugin QA | Done | P1 | docs | M3 |

### v2 — Visual target (M4–M6)

| Issue | Title | Status | Prio | Area | Milestone |
|-------|-------|--------|------|------|-----------|
| #16 | Revive card borders: rim around every deck card (render-path-safe) | Todo | P1 | visual | M4 |
| #17 | Revive wallpaper canvas: dimmed/zoomed background behind the deck | Todo | P1 | visual | M4 |
| #18 | Revive card frost: frosted-glass veil over back cards | Todo | P1 | visual | M4 |
| #19 | Tune depth falloff to match visual target (scale/opacity curve) | Todo | P1 | visual | M4 |
| #20 | Dim-on-startup: deck renders immediately without click-to-focus | Todo | P1 | feature | M4 |
| #21 | Layer exclusions: per-class rules to never depth (docks, floats) | Todo | P1 | feature | M4 |
| #23 | Animation easing/bezier per depth transition | Todo | P2 | feature | M5 |
| #24 | Shadow intensity/color config for background layers | Todo | P2 | feature | M5 |
| #25 | Multi-monitor participation policy | Todo | P2 | feature | M5 |
| #26 | Configurable depth→transform curves (scale/opacity as f(layer)) | Todo | P2 | feature | M5 |
| #27 | PKGBUILD for Ryoku (`ryoku-focusZ`) | Todo | P0 | ops | M6 |
| #28 | Module sync: `focusz.lua` tracks plugin version | Todo | P1 | ops | M6 |
| #30 | Defaults in `hyprland.lua`: module config values | Todo | P1 | ops | M6 |

> Issue numbers are GitHub's. #15 is a throwaway "Test issue"; the old table on
> this board was written against a shifted numbering and mismatched every card
> from #15 up. Reconciled 2026-10-06.

### Removed / Archived

| Issue | Title | Status | Note |
|-------|-------|--------|------|
| #10 | Pull the wallpaper back into the depth scene | Removed | Superseded by #16 (v2 canvas) |
| #11 | Strengthen the Z-axis perspective (scale/opacity falloff) | Removed | Superseded by #18 |
| #14 | Fix: wallpaper-canvas dimmed the wrong layer | Removed | Superseded by #16 |

### Documentation

| Issue | Title | Status | Prio | Area | Milestone |
|-------|-------|--------|------|------|-----------|
| #5 | Headless tests: impossible, document the manual loop | Todo | P1 | ci/docs | M3 |
| #6 | Rebuild flow on Hyprland upgrades (ABI lock) | Todo | P1 | ops | M6 |
| #7 | Per-layer blur radius (API limitation) | Blocked | P2 | feature | M4 |

## Visual Parity Status

**Current parity:** ~60% (core depth effect works, visual polish missing)
**Target:** `~/Downloads/deep_1-1_000.zip` (140 PNGs)
**Analysis completed:** 2026-08-13
**Action plan:** tracked in `docs/ROADMAP.md` and the kanban (M4, 4 phases)

### What Works ✅

| Feature | Status | Config | Notes |
|---------|--------|--------|-------|
| Card scaling | ✅ | `layer1Scale=0.70`, `layer2Scale=0.50` | Back cards get smaller boxes |
| Opacity reduction | ✅ | `layer1Opacity=0.85`, `layer2Opacity=0.70` | Per-layer alpha |
| Card positioning | ✅ | `card_edge_scatter=true` | Edge or around-focused scatter |
| Focus promotion | ✅ | — | Clicking card promotes to Layer 0 |
| Stack rebuilding | ✅ | — | Identity check prevents unnecessary rebuilds |
| Workspace isolation | ✅ | — | Only anchor monitor's active workspace |
| Fullscreen skip | ✅ | — | Fullscreen windows not affected |
| Unit tests | ✅ | — | 18 tests passing |

### What's Missing ❌

| Feature | Status | Issue | Priority | Phase |
|---------|--------|-------|----------|-------|
| Card borders | ❌ | #16 | P1 | Phase 2 |
| Frosted glass | ❌ | #18 | P1 | Phase 2 |
| Canvas plate | ❌ | New needed | P2 | Phase 3 |
| Wallpaper canvas | ❌ | #17 | P1 | Phase 3 |
| Depth shadows | ❌ | #24 | P2 | Phase 3 |
| Animation easing | ❌ | #23 | P2 | Phase 3 |

### Implementation Phases

| Phase | Duration | Features | Status |
|-------|----------|----------|--------|
| Phase 1: Quick Wins | 1-2 days | Tune scale/opacity parameters | Ready |
| Phase 2: Render-Path-Safe | 3-5 days | Card borders, frosted glass | Blocked on #16 |
| Phase 3: Advanced | 5-7 days | Canvas plate, wallpaper, shadows | Blocked on Phase 2 |
| Phase 4: Integration | 2-3 days | Config validation, docs, release | Blocked on Phase 3 |

### Target Frame Analysis

| Frames | State | Visual Characteristics |
|--------|-------|------------------------|
| 000–032 | Flat (pre-depth) | Single terminal, gray background, no depth |
| 033–045 | Transition | Two windows visible, depth effect activating |
| 046–108 | Active depth | 3+ cards, strong scale falloff, blurred back cards |
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

## Notes

- **Visual target:** `~/Downloads/deep_1-1_000.zip` (140 PNGs defining the exact
  depth/blur/canvas look). Frames show: flat gray background, terminal windows
  with "layer 3/2/1" text, landscape painting, strong depth scale falloff, cards
  scattered across workarea, frosted glass blur behind back cards.
- **Render-path-free constraint (M1):** the plugin must not call `addPassElement`,
  attach `IWindowTransformer`, or draw decorations. All crashes in v0.56.2 came
  from the render path. Issues #16–#18 need a new safe render mechanism.
- **Dead code:** removed 2026-10-06 — the empty `drawCardBorders()`/`drawCanvas()`/
  `drawCardFrost()`/`onRenderStage()` bodies, the never-attached
  `CDepthShadowDecoration`, and the 18 config keys that were registered but
  never read. Issues #16–#18 revive those features **with** their keys, not
  before.
- **ABI lock (#6):** top operational risk — plugin unloads on any Hyprland
  version-hash mismatch. Every collaborator must rebuild after upgrades.
- **Per-layer blur radius (#7):** blocked by plugin API — blur size is the global
  `blur:size` setting; only an on/off `noblur` rule exists.
- **Card borders via decoration:** safest render-path approach — a dedicated
  decoration (the old `CDepthShadowDecoration` slot) drawing with
  `renderRect`, attached for real this time.
- **Testing infrastructure:** Google Test with 18 unit tests covering pure
  functions (randSeed, clampToWorkarea, scale calculations).
  Run with `./run-tests.sh` or `cmake --build build-tests && cd build-tests && ctest`.
- **CI integration:** Unit tests run automatically on every push/PR via
  `.github/workflows/build.yml`. All tests passing ✅.

## Operations

### Project-scope access

Board changes go through the `gh` CLI. Every collaborator needs the `project`
scope on their token:

```bash
gh auth refresh -s project
```

### Recreating the board (disaster recovery)

If the board is ever deleted, recreate it as follows. `gh project create`
ships a template that already includes a `Status` field — do **not** create a
second one.

```bash
gh project create --owner Lowingx --title "focusZ Kanban" --format json
gh project link <project> --owner Lowingx --repo Lowingx/hypr-focuZ
gh project item-add <project> --owner Lowingx --url <issue-url>
```

> **Gotcha:** adding a status option via `updateProjectV2Field` replaces the
> whole option list, so when rebuilding the field, pass **all** options in one
> call: `Todo`, `In Progress`, `In Review`, `Done`.

### Milestones, priorities, areas

Cards inherit their `Prio` / `Area` / `Milestone` from the `ROADMAP.md` tables
and the issue labels (`priority:*`, `area:*`, `status:*`). Keep the two
documents in sync when work is reprioritized.
