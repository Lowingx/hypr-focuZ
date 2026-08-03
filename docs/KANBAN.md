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
   loads in a live Hyprland session (there is no CI or test suite yet; see
   AGENTS.md). Close the issue once the repo docs reflect the state.
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
| #4 | Record architecture decisions as ADRs | In Progress | P0 | docs | M1.1 |
| #5 | Headless tests: impossible, document the manual loop | Todo | P1 | ci/docs | M2.1 |
| #6 | Rebuild flow on Hyprland upgrades (ABI lock) | Todo | P1 | ops | M3.1 |
| #7 | Per-layer blur radius (API limitation) | Blocked | P2 | feature | M4.1 |
| #8 | Add CI build verification | Todo | P1 | ci | M2.2 |

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

## Notes

- **ABI lock (#6)** is the top operational risk: the plugin unloads on any
  `hyprland` version-hash mismatch. The whole team must rebuild after every
  package upgrade.
- **Per-layer blur radius (#7)** is blocked by the plugin API — blur size is
  the global `blur:size` setting; only an on/off `noblur` rule exists. The
  `layer_*_blur` config toggles are registered but inert.
- **Verification (#5)** is build + load in a live session; headless testing is
  impossible for a Hyprland plugin.
