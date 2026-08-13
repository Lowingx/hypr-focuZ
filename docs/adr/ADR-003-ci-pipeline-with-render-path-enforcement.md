# ADR-003: CI Pipeline with Render-Path Enforcement

- **Status**: accepted
- **Date**: 2026-08-12
- **Deciders**: Lowingx
- **Tags**: ci, safety, hyprland-plugin

## Context

After the render-path-free architecture decision (ADR-001), the project needs automated enforcement to prevent render-path code from being reintroduced. Manual code review is insufficient — a single `addPassElement` call can crash the compositor.

## Decision

Implement a three-workflow CI pipeline with static analysis gates:

### 1. CI Workflow (`build.yml`) — Every push/PR

- Arch Linux container with Hyprland 0.56.2
- `make check` (rebuild with `-Werror`)
- CMake alternative build
- Security scan (dangerous functions: `system`, `popen`, `exec`, `strcpy`, `sprintf`)
- **Render-path check**: `grep -rn "addPassElement|IWindowTransformer" src/ | grep -v "//|DISABLED|dead code"`
- Upload `.so` artifact

### 2. PR Validation (`pr-validation.yml`) — PRs only

- Build + `-Werror`
- Render-path check on diff (only new lines)
- Config consistency check
- Documentation drift check

### 3. Release Workflow (`release.yml`) — Tags `v*`

- Render-path check + security scan
- Build with `-Werror`
- Tag format validation
- Changelog generation
- GitHub Release with checksums

## Consequences

### Positive

- **Render-path violations caught before merge** — CI fails if `addPassElement` or `IWindowTransformer` appears in source
- **Security baseline** — dangerous function scan prevents injection vectors
- **Release integrity** — SHA256 checksums and tag validation
- **Dual build system** — Make and CMake both tested, preventing build drift

### Negative

- **CI time** — Arch container setup + full rebuild takes ~90s
- **Node 20 deprecation warnings** — GitHub Actions runner still uses Node 20 for checkout/upload

### Neutral

- The render-path check uses `grep -v "//|DISABLED|dead code"` to allow commented-out or documented dead code
- The security scan is advisory (warns but doesn't fail) — dangerous functions may be used intentionally
- PR validation only checks diff, not full source — catches new violations but not pre-existing ones

## Links

- `.github/workflows/build.yml` — CI workflow
- `.github/workflows/pr-validation.yml` — PR validation
- `.github/workflows/release.yml` — Release workflow
- ADR-001 (render-path-free architecture)
- ADR-002 (micro-optimizations)
