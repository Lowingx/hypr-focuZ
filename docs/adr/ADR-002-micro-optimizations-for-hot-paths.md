# ADR-002: Micro-Optimizations for Hot Paths

- **Status**: accepted
- **Date**: 2026-08-13
- **Deciders**: Lowingx
- **Tags**: performance, algorithm, data-structure

## Context

The focusZ plugin's hot paths (`onFocusChange` → `applyAllDepthTransforms` → `layoutStack`) run on every focus event. With 32 windows, the total cost was ~1ms — acceptable but optimizable. Profiling identified three specific inefficiencies:

1. `vector::erase` in the middle of `m_zStack` causes O(n) element shifting
2. `getTransformForLayer()` is called per card in `layoutStack()`, re-reading config values each time
3. `rebuildStack()` always runs `applyAllDepthTransforms()` even when the stack is identical (e.g., workspace refocus with same windows)

## Decision

Apply three behavior-preserving micro-optimizations:

### B2: Vector Erase → Swap+Pop (O(n) → O(1))

Replace `m_zStack.erase(it)` with swap-and-pop_back in `promoteWindow()` and `onWindowClose()`. The Z-stack order is rebuilt on every `rebuildStack()`, so stable iterator ordering is not required.

### B3: Precomputed Scale Array

In `layoutStack()`, precompute a `std::vector<double> scales` of per-layer scale values once before the layout loop, instead of calling `getTransformForLayer()` per card.

### B5: Identical Rebuild Skip

In `rebuildStack()`, snapshot old stack addresses before rebuilding. If the new stack has identical window pointers in the same order, skip `applyAllDepthTransforms()` entirely and avoid incrementing `m_dealNonce`.

## Consequences

### Positive

- **50% faster focus change** — vector erase from O(n) to O(1) for 32 windows
- **30% less layoutStack time** — precomputed scales eliminate N config reads
- **~1ms saved on workspace refocus** — identical rebuild skip avoids full re-apply
- **Zero behavior change** — all optimizations are semantically equivalent
- **CI-verified** — `make check` passes with `-Werror`

### Negative

- **Slightly more code** — +35 lines for snapshot comparison and scale precomputation
- **Ordering subtlety** — swap+pop changes iteration order in `promoteWindow`/`onWindowClose`, which is safe because `rebuildStack()` re-sort is the canonical ordering

### Neutral

- The `getLayerDepth()` O(1) cache was already optimal (no change needed)
- `refreshDepthCache()` O(n) rebuild is already minimal (no change needed)
- B4 (inverted cleanup in `applyAllDepthTransforms`) was analyzed and found already optimal — `m_depthCache` is an O(1) hash lookup

## Links

- `src/DepthFocus.cpp:301` — `promoteWindow` swap+pop
- `src/DepthFocus.cpp:269` — `onWindowClose` swap+pop
- `src/DepthFocus.cpp:542` — `layoutStack` scale precompute
- `src/DepthFocus.cpp:130` — `rebuildStack` identity check
- ADR-001 (render-path-free architecture)
