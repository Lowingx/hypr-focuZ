# focusZ Kanban

Working board for the **`Lowingx/hypr-focuZ`** repository — a Hyprland plugin
implementing a Z-axis depth-focus layout.

> **Operational view.** For *why* each task exists, its scope, and its
> definition of done, read the **[ROADMAP](ROADMAP.md)**. This board mirrors
> the roadmap milestones; labels map to the taxonomy defined there.

## Board

- **Platform:** GitHub Projects (v2)
- **Owner:** `Lowingx`
- **Repository:** `Lowingx/hypr-focuZ`
- **Board name:** `focusZ Kanban`
- **Board URL:** https://github.com/users/Lowingx/projects/3
- **Status field:** `Todo` → `In Progress` → `In Review` → `Done`

### One-time setup (already done on this machine)

```bash
gh auth refresh -s project            # grant project scopes to the token
gh project create --owner Lowingx --title "focusZ Kanban" --format json
gh project link <project> --owner Lowingx --repo Lowingx/hypr-focuZ
# "Status" single-select field ships with the project template; if you recreate
# it from scratch, updateProjectV2Field replaces the option list, so pass ALL
# options at once: Todo, In Progress, In Review, Done.
gh project item-add <project> --owner Lowingx --url <issue-url>
gh project item-edit <project> --owner Lowingx --field-id <status-field-id> \
  --single-select-option-id <option-id>
```

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

## Workflow

1. **New work** — open an issue, add it to the board, set status `Todo`.
2. **Active work** — move to `In Progress` while implementing.
3. **Under review** — move to `In Review` once a PR (or commit) exists.
4. **Verified** — move to `Done` only after the change builds and loads in a live
   Hyprland session (there is no CI or test suite; see AGENTS.md).
5. Close the issue once the state is reflected in the repo docs.

## Notes

- **ABI lock (#6)** is the top operational risk: the plugin unloads on any
  `hyprland` version-hash mismatch. Rebuild after every package upgrade.
- **Per-layer blur radius (#7)** is blocked by the plugin API — blur size is the
  global `blur:size` setting; only an on/off `noblur` rule exists. The
  `layer_*_blur` config toggles are registered but inert.
- **Verification (#5)** is build + load in a live session; headless testing is
  impossible for a Hyprland plugin.
