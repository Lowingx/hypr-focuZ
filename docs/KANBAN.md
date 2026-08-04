# focusZ Kanban

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
   loads in a live Hyprland session. CI (`.github/workflows/build.yml`) is the
   build gate; there is still no live-session test. Close the issue once the
   repo docs reflect the state.
5. **Retrospect** — at milestone boundaries, reconcile the board against
   `ROADMAP.md` and archive stale cards.

Cards that look stuck get a `help-wanted` label. Anything that needs the
plugin API to grow first gets `status:blocked`.

## Current items

| Issue | Title | Status | Prio | Area | Milestone |
|-------|-------|--------|------|------|-----------|
| #1 | Anchor the depth stack to the focused monitor | Done | P0 | core | M0.2 |
| #2 | Restore windows on evict / disable / close | Done | P0 | core | M0.3 |
| #3 | Keep the per-frame render hook O(1) | Done | P0 | core | M0.4 |
| #4 | Record architecture decisions as ADRs | Todo | P0 | docs | M1.1 |
| #5 | Headless tests: impossible, document the manual loop | Todo | P1 | ci/docs | M2.1 |
| #6 | Rebuild flow on Hyprland upgrades (ABI lock) | Todo | P1 | ops | M3.1 |
| #7 | Per-layer blur radius (API limitation) | Blocked | P2 | feature | M4.1 |
| #8 | Add CI build verification | Done | P1 | ci | M2.2 |
| #9 | Blur behind background cards is not visible | Done | P1 | feature | M4.1 |
| #10 | Pull the wallpaper back into the depth scene | Done | P1 | feature | M4.3 |
| #11 | Strengthen the Z-axis perspective (scale/opacity falloff) | Done | P1 | feature | M4.3 |
| #12 | CD: tagged releases with artifact + `make check` gate | Done | P1 | ci | M3.3 |
| #13 | Fix: deck mixed windows from different workspaces on the same monitor | Done | P0 | core | M0.2 |
| #14 | Fix: wallpaper-canvas dimmed the wrong layer (1×1 pill-inhibit) | Done | P0 | core | M4.3 |

## Operations

### Project-scope access

Board changes go through the `gh` CLI. Every collaborator needs the `project`
scope on their token:

```bash
gh auth refresh -h github.com -s project
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

## Notes

- **ABI lock (#6)** is the top operational risk: the plugin unloads on any
  `hyprland` version-hash mismatch. The whole team must rebuild after every
  package upgrade.
- **Per-layer blur radius (#7)** is blocked by the plugin API — blur size is
  the global `blur:size` setting; only an on/off `noblur` rule exists. The
  `layer_*_blur` config toggles are registered but inert.
- **CI (#8)** ships as `.github/workflows/build.yml` (Arch container, `hyprland`
  package) plus `make check` — a clean rebuild with `-Werror` so any warning
  fails the build. Verified green.
- **CD (#12)** ships as `.github/workflows/release.yml`: on tags `v*` it reruns
  `make check`, rebuilds, and publishes `libfocusZ.so` + `SHA256SUMS` as a
  GitHub Release via `gh`.
- **Blur not visible (#9)** — root cause was back cards placed fully inside the
  opaque focused card's footprint. Fixed by random-angle protrusion past the
  front card; live-verified.
- **Wallpaper pull (#10)** — implemented by dimming the wallpaper layer's fade
  alpha via `LS_ALPHA_FADE`; geometry deliberately untouched. The dim is now
  configurable (`plugin:focusZ:wallpaper_dim`, default 0.5) so the wallpaper
  acts as a darker canvas behind the deck. Live-verified.
- **Perspective (#11)** — scale/opacity falloff shipped (layer 1: 0.62,
  layer 2: 0.42) and later reworked to interpolate from layer 2 down to the
  floor (0.22 / 0.05) across the remaining `max_layers` — a fixed per-layer
  step had collapsed every window past depth 3 to the floor, so deep cards
  looked identical. Live-verified.
- **Workspace mixing (#13)** — the stack was anchored to the monitor only, so
  focusing a window on another workspace of the same monitor promoted it onto
  the old deck. The stack now records its workspace and rebuilds when it
  changes. Live-verified.
- **Wallpaper-canvas bug (#14)** — `pullWallpaper` picked the first visible
  surface on the BACKGROUND layer, which was quickshell's 1×1 `pill-inhibit`
  helper, dimming a 1-px sprite instead of the wallpaper. It now matches a
  known wallpaper namespace (awww-daemon, mpvpaper, …) or falls back to the
  first fullscreen surface. Live-verified.
- **Verification (#5)** is build + load in a live session; headless testing is
  impossible for a Hyprland plugin.
