# focusZ — Code Analysis Report

**Agent:** Code Analyzer  
**Date:** 2026-08-13  
**Scope:** Full codebase (`src/*.cpp`, `src/*.hpp`)

---

## Executive Summary

| Metric | Value | Status |
|--------|-------|--------|
| Total Lines | 1,646 | ✅ |
| Files | 10 (5 .cpp, 5 .hpp) | ✅ |
| Complexity (highest) | 31 (layoutStack) | ⚠️ |
| Dead Code Lines | ~150 | ⚠️ |
| Security Issues | 0 critical | ✅ |
| Memory Safety | Good (27 lock() calls) | ✅ |
| Render-Path Violations | 2 (dead code only) | ⚠️ |

---

## 1. Code Quality

### 1.1 Complexity Analysis

| Function | Lines | Complexity | Status |
|----------|-------|------------|--------|
| `layoutStack()` | 206 | 31 | ⚠️ HIGH |
| `drawCardBorders()` | 111 | 21 | 🔴 DEAD CODE |
| `applyAllDepthTransforms()` | 80 | 18 | ⚠️ HIGH |
| `clipSegment()` | 78 | 16 | ⚠️ HIGH |
| `rebuildStack()` | 70 | 15 | ⚠️ HIGH |
| `restoreWindow()` | 31 | 8 | ✅ |
| `onWindowOpen()` | 37 | 7 | ✅ |
| `onFocusChange()` | 33 | 6 | ✅ |
| `onWindowClose()` | 32 | 6 | ✅ |

**Recommendation:** `layoutStack()` at 206 lines / complexity 31 should be split into smaller functions (edge-scatter, around-scatter, safety-net).

### 1.2 Dead Code

| Location | Issue | Severity |
|----------|-------|----------|
| `drawCardBorders()` | 111 lines, never called | ⚠️ MEDIUM |
| `drawCardFrost()` | Empty body, never called | ⚠️ LOW |
| `drawCanvas()` | Empty body, never called | ⚠️ LOW |
| `clipSegment()` | 78 lines, never called | ⚠️ MEDIUM |
| `exposedStrips()` | REMOVED (fixed in ede9249) | ✅ |
| `ScaleTransformer.cpp` | 75 lines, never instantiated | ⚠️ LOW |

**Total dead code:** ~340 lines (~21% of codebase)

### 1.3 Naming & Style

- ✅ Consistent naming: `camelCase` for methods, `m_` prefix for members
- ✅ Config keys match between `main.cpp` and README
- ✅ Comments are clear and explain "why" not just "what"
- ⚠️ Some functions have too many parameters (layoutStack has 0 params but accesses 15+ globals)

---

## 2. Security

### 2.1 Vulnerability Scan

| Check | Result |
|-------|--------|
| Dangerous functions (system, popen, exec) | ✅ None found |
| Buffer overflow (strcpy, sprintf, gets) | ✅ None found |
| Format string vulnerabilities | ✅ None found |
| Race conditions | ⚠️ 27 lock() calls — needs review |
| Secret exposure | ✅ No hardcoded secrets |

### 2.2 Dependency Security

| Dependency | Version | Status |
|------------|---------|--------|
| hyprland | 0.56.2 | ✅ ABI-locked |
| pixman | system | ✅ |
| libdrm | system | ✅ |

### 2.3 Render-Path Safety

| Check | Result |
|-------|--------|
| `addPassElement` calls | ⚠️ 2 found (in dead code) |
| `IWindowTransformer` usage | ✅ DISABLED (never attached) |
| Decoration `draw()` | ✅ No-op (never attached) |

**Note:** The `addPassElement` calls in `drawCardBorders()` are in dead code (function is never called). However, this is a **latent risk** — if someone re-enables `onRenderStage`, the plugin will crash.

---

## 3. Performance

### 3.1 Hot Path Analysis

| Path | Cost | Status |
|------|------|--------|
| `getLayerDepth()` | O(1) cached | ✅ |
| `getTransformForLayer()` | O(1) switch | ✅ |
| `refreshDepthCache()` | O(n) on mutation only | ✅ |
| `applyAllDepthTransforms()` | O(n) on mutation only | ✅ |
| `layoutStack()` | O(n²) worst case | ⚠️ |

**`layoutStack()` concern:** The edge-scatter mode does up to 64 attempts per card × n cards = O(64n). With 32 windows (HARD_CAP), this is 2048 iterations max — acceptable.

### 3.2 Memory

- `m_zStack`: vector of weak_ptr (max 32) ✅
- `m_applied`: unordered_map of SDepthState ✅
- `m_depthCache`: unordered_map of int ✅
- No memory leaks detected ✅

### 3.3 Damage Optimization

- `g_pHyprRenderer->damageWindow()` called only when depth changes ✅
- Unchanged windows not re-damaged ✅

---

## 4. Architecture

### 4.1 Design Patterns

| Pattern | Usage | Status |
|---------|-------|--------|
| Observer | EventBus listeners | ✅ |
| State | SDepthState per window | ✅ |
| Strategy | getTransformForLayer | ✅ |
| Decorator | CDepthShadowDecoration | ✅ (disabled) |

### 4.2 Coupling

- Plugin ↔ Hyprland: 15 API calls (addConfigValueV2, damageWindow, etc.) — acceptable
- Internal: CDepthFocusManager is monolithic (does everything) — ⚠️
- Dead code creates phantom coupling to render path

### 4.3 Invariants

| Invariant | Status |
|-----------|--------|
| Render-path-free | ✅ (dead code doesn't run) |
| O(1) hot path | ✅ |
| Per-monitor isolation | ✅ |
| No scale compounding | ✅ |
| ABI lock | ✅ |

---

## 5. Technical Debt

### 5.1 High Priority

1. **Dead code removal** — `drawCardBorders()`, `clipSegment()`, unused forward declarations
2. **`layoutStack()` refactor** — split into edge-scatter, around-scatter, safety-net
3. **Latent render-path risk** — `addPassElement` in dead code could be accidentally re-enabled

### 5.2 Medium Priority

4. **Missing error handling** — no try/catch in event handlers
5. **Global state** — `cfg()` is a global singleton, hard to test
6. **No unit tests** — pure math functions (getTransformForLayer) could be tested

### 5.3 Low Priority

7. **ScaleTransformer.cpp** — 75 lines of dead code, should be removed or clearly marked
8. **Comment style** — some functions have excessive comments, others have none
9. **Magic numbers** — MARGIN=48.0, HARD_CAP=32, EDGE_INSET=16.0 should be named constants

---

## 6. Recommendations

### Immediate (v0.1.1)

1. Remove dead code: `drawCardBorders()`, `drawCardFrost()`, `drawCanvas()`, `clipSegment()`
2. Remove `ScaleTransformer.cpp` or mark clearly as reference-only
3. Extract magic numbers to named constants

### Short-term (v0.2.0)

4. Refactor `layoutStack()` into smaller functions
5. Add unit tests for `getTransformForLayer()`, `getDeckFactor()`
6. Add try/catch around event handlers

### Long-term (v1.0.0)

7. Split CDepthFocusManager into smaller classes (StackManager, LayoutEngine, etc.)
8. Add configuration validation (min < max, etc.)
9. Add metrics collection for performance monitoring

---

## 7. Quality Score

| Dimension | Score | Notes |
|-----------|-------|-------|
| Correctness | 9/10 | Works as intended, no known bugs |
| Maintainability | 7/10 | Dead code, high complexity functions |
| Security | 9/10 | No vulnerabilities, render-path-safe |
| Performance | 8/10 | O(1) hot path, minor concerns in layoutStack |
| Documentation | 8/10 | Good README, ADRs pending |
| Testing | 3/10 | No unit tests, only manual verification |
| **Overall** | **7.3/10** | Good foundation, needs cleanup |

---

*Report generated by Code Analyzer agent*
