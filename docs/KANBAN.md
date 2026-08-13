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
4. **Verify together** — a card reaches `Done` only when the change builds and
   loads in a live Hyprland session (there is no CI or test suite yet; see
   AGENTS.md). Close the issue once the repo docs reflect the state.
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
| #4 | Record architecture decisions as ADRs | In Progress | P0 | docs | M2 |
| #8 | Add CI build verification | Done | P1 | ci | M3 |
| #13 | Fix: deck mixed windows from different workspaces on the same monitor | Done | P0 | core | M0 |

### v2 — Visual target (M4–M6)

| Issue | Title | Status | Prio | Area | Milestone |
|-------|-------|--------|------|------|-----------|
| #15 | Revive card borders: rim around every deck card (render-path-safe) | Todo | P1 | visual | M4 |
| #16 | Revive wallpaper canvas: dimmed/zoomed background behind the deck | Todo | P1 | visual | M4 |
| #17 | Revive card frost: frosted-glass veil over back cards | Todo | P1 | visual | M4 |
| #18 | Tune depth falloff to match visual target (scale/opacity curve) | Todo | P1 | visual | M4 |
| #19 | Dim-on-startup: deck renders immediately without click-to-focus | Todo | P1 | feature | M4 |
| #20 | Layer exclusions: per-class rules to never depth (docks, floats) | Todo | P1 | feature | M4 |
| #21 | Per-layer blur radius (API limitation — reserved, inert) | Blocked | P2 | feature | M4 |
| #22 | Animation easing/bezier per depth transition | Todo | P2 | feature | M5 |
| #23 | Shadow intensity/color config for background layers | Todo | P2 | feature | M5 |
| #24 | Multi-monitor participation policy | Todo | P2 | feature | M5 |
| #25 | Configurable depth→transform curves (scale/opacity as f(layer)) | Todo | P2 | feature | M5 |
| #26 | PKGBUILD for Ryoku (`ryoku-focusZ`) | Todo | P0 | ops | M6 |
| #27 | Module sync: `focusz.lua` tracks plugin version | Todo | P1 | ops | M6 |
| #28 | ABI rebuild flow: detect hyprland upgrade, trigger rebuild | Todo | P1 | ops | M6 |
| #29 | Defaults in `hyprland.lua`: module config values | Todo | P1 | ops | M6 |

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

## Notes

- **Visual target:** `~/Downloads/deep_1-1_000.zip` (140 PNGs defining the exact
  depth/blur/canvas look). Frames show: dark wallpaper canvas behind the deck,
  frosted glass blur on back cards, strong depth scale falloff, card borders/rims.
- **Render-path-free constraint (M1):** the plugin must not call `addPassElement`,
  attach `IWindowTransformer`, or draw decorations. All crashes in v0.56.2 came
  from the render path. Issues #15–#17 need a new safe render mechanism.
- **Dead code:** `drawCardBorders()`, `drawCanvas()`, `drawCardFrost()`, and
  `onRenderStage()` are all empty/no-op bodies. Config keys for these features
  are registered but inert. Issues #15–#17 revive them safely.
- **ABI lock (#6):** top operational risk — plugin unloads on any Hyprland
  version-hash mismatch. Every collaborator must rebuild after upgrades.
- **Per-layer blur radius (#21):** blocked by plugin API — blur size is the global
  `blur:size` setting; only an on/off `noblur` rule exists.
- **Verification (#5):** build + load in a live session; headless testing is
  impossible for a Hyprland plugin.

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
