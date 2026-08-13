# focusZ — Security Audit Report

**Agent:** Security Audit  
**Date:** 2026-08-13  
**Scope:** Full codebase (`src/*.cpp`, `src/*.hpp`)  
**Tool:** Manual analysis + @claude-flow/cli secrets scan

---

## Executive Summary

| Category | Findings | Severity |
|----------|----------|----------|
| Critical | 0 | — |
| High | 1 | Render-path latent risk |
| Medium | 3 | Missing validation, no error handling, config injection |
| Low | 2 | Dead code attack surface, no input sanitization |
| **Total** | **6** | — |

---

## 1. Memory Safety

### 1.1 Pointer Management

| Type | Count | Status |
|------|-------|--------|
| Raw `new` | 0 | ✅ |
| Raw `delete` | 0 | ✅ |
| `unique_ptr` | 0 | ✅ |
| `shared_ptr` | 0 | ✅ |
| `weak_ptr` (via Hyprland) | 27 `.lock()` calls | ⚠️ See below |

**Analysis:** The plugin uses Hyprland's `PHLWINDOW` (which is `std::weak_ptr<CHyprWindow>`). All `.lock()` calls are followed by null checks:

```cpp
if (auto w = st.window.lock()) {
    // safe to use w
}
```

**Finding:** ✅ All 27 `.lock()` calls are properly guarded. No dangling pointer risk.

### 1.2 Lifetime Management

| Object | Lifetime | Status |
|--------|----------|--------|
| `CDepthFocusManager` | Singleton, lives as long as Hyprland | ✅ |
| `SDepthState` | Stored in `m_applied`, removed on window close | ✅ |
| `m_zStack` | `vector<weak_ptr>`, windows auto-expire | ✅ |
| `CDepthShadowDecoration` | Attached to window, dies with it | ✅ |
| `CScaleTransformer` | Dead code, never instantiated | ✅ |

**Finding:** ✅ No lifetime issues. All objects are properly scoped.

---

## 2. Race Conditions

### 2.1 Concurrency Model

| Mechanism | Count | Usage |
|-----------|-------|-------|
| `std::lock_guard<std::mutex>` | 3 | DebugLog only |
| `std::mutex` | 1 | `g_mutex` in DebugLog |
| Event hooks | 8 | Sequential (Hyprland main thread) |

**Analysis:** Hyprland event hooks run on the main compositor thread. The plugin's event handlers (`onWindowOpen`, `onWindowClose`, `onFocusChange`, `layoutStack`) are called sequentially by Hyprland's event loop.

**Finding:** ✅ No race conditions. All plugin code runs on Hyprland's main thread. The only mutex is in `DebugLog.cpp` for file I/O, which is correct.

### 2.2 Reentrancy Risk

| Function | Reentrant? | Risk |
|----------|------------|------|
| `layoutStack()` | Yes (called from multiple hooks) | ⚠️ Low |
| `refreshDepthCache()` | Yes (called from layoutStack + onFocusChange) | ⚠️ Low |
| `applyAllDepthTransforms()` | Yes (called from layoutStack + onWindowOpen/Close) | ⚠️ Low |

**Finding:** ⚠️ Low risk. Hyprland's event loop is single-threaded, so reentrancy won't occur in practice. However, if Hyprland ever moves to multi-threaded event processing, these functions could be problematic.

---

## 3. Configuration Injection

### 3.1 Config Keys

| Type | Count | Validation |
|------|-------|------------|
| `Bool` | 4 | ✅ Hyprland validates |
| `Int` | 3 | ⚠️ No min/max in plugin |
| `Float` | 14 | ⚠️ No min/max in plugin |
| `String` | 6 | ⚠️ No content validation |

### 3.2 Config Validation

**Current state:** Config values are read directly from Hyprland's config system:

```cpp
cfg().layer1Scale = std::clamp(*cfg().layer1Scale, 0.1f, 1.5f);
cfg().layer2Scale = std::clamp(*cfg().layer2Scale, 0.1f, 1.5f);
cfg().frontScale = std::clamp(*cfg().frontScale, 0.1f, 1.5f);
```

**Issues found:**

1. **Partial validation:** Only 3 of 14 float values are clamped. The rest (`layer1Opacity`, `layer2Opacity`, `animationSpeed`, `wallpaperDim`, `wallpaperZoom`, `canvasZoomFloor`, `canvasDimFloor`, `canvasPlateAlpha`, `canvasPlateAlphaMax`, `canvasShadowBoost`, `borderWidth`) have no bounds checking.

2. **No string validation:** `plateFrost`, `cardBorder`, `borderColor` strings are not validated. Malformed hex values could cause rendering issues.

3. **No type validation:** If a user sets `layer1Scale = "hello"`, Hyprland's config system will reject it, but the plugin doesn't handle the error gracefully.

**Severity:** ⚠️ MEDIUM — A malicious or malformed config could cause unexpected behavior (negative scales, NaN values, division by zero).

### 3.3 Potential Exploits

| Attack Vector | Impact | Likelihood |
|---------------|--------|------------|
| Negative scale values | Windows rendered at wrong size | Low |
| NaN/Inf values | Rendering glitches, potential crash | Low |
| Extremely large values | Memory pressure, performance degradation | Low |
| Empty strings | Null pointer dereference | Low |
| Malformed hex colors | Rendering artifacts | Low |

**Finding:** ⚠️ MEDIUM — Config validation is incomplete. Recommend adding bounds checking for all numeric values.

---

## 4. Render-Path Safety

### 4.1 Critical Check: `addPassElement` Calls

```cpp
// DepthFocus.cpp:591
g_pHyprRenderer->addPassElement(Hyprutils::Memory::makeUnique<CBorderPassElement>(data));

// DepthFocus.cpp:631
g_pHyprRenderer->addPassElement(Hyprutils::Memory::makeUnique<CRectPassElement>(data));
```

**Status:** These calls are inside `drawCardBorders()`, which is **dead code** (never called from `onRenderStage`). However:

1. The function body is intact (111 lines)
2. It's still declared in the header
3. If someone re-enables `onRenderStage`, the plugin will crash

**Severity:** ⚠️ HIGH — Latent crash risk. Dead code should be removed.

### 4.2 Decorator Safety

```cpp
// DepthShadow.cpp:35-38
// DISABLED — g_pHyprRenderer->drawShadow() touches m_renderData.pMonitor
// which is only valid inside RENDER_POST_WINDOWS.  Adding the shadow
// decoration would crash the compositor if the window is ever painted
// outside that stage, so this decoration is a no-op placeholder...
```

**Status:** ✅ Decorator is a no-op. `addDamage()` returns immediately.

---

## 5. Secrets & Sensitive Data

### 5.1 Scan Results

| Check | Result |
|-------|--------|
| Hardcoded API keys | ✅ None found |
| Hardcoded passwords | ✅ None found |
| Hardcoded tokens | ✅ None found |
| Private keys | ✅ None found |
| Connection strings | ✅ None found |

### 5.2 Debug Logging

```cpp
DebugLog::log("[focusZ] stack: ...");
DebugLog::log("[focusZ] depth: ...");
DebugLog::log("[focusZ] focus: ...");
```

**Finding:** Debug logs contain window pointers and depth values. These are not sensitive but could leak internal state in production. Recommend conditional compilation for debug logs.

---

## 6. Dependency Security

### 6.1 System Dependencies

| Dependency | Version | CVE Status |
|------------|---------|------------|
| hyprland | 0.56.2 | ✅ ABI-locked |
| pixman | system | ✅ |
| libdrm | system | ✅ |
| CMake | build-only | ✅ |

### 6.2 Build-Time Dependencies

| Dependency | Risk |
|------------|------|
| `pkg-config` | ✅ Low |
| `cmake` | ✅ Low |
| `gcc` | ✅ Low |

**Finding:** ✅ No known CVEs in dependencies.

---

## 7. Recommendations

### Immediate (v0.1.1)

1. **Remove dead code** — `drawCardBorders()`, `drawCardFrost()`, `drawCanvas()`, `clipSegment()` to eliminate latent crash risk
2. **Add bounds checking** for all config values (clamp to reasonable ranges)

### Short-term (v0.2.0)

3. **Add error handling** — try/catch around event handlers to prevent plugin crashes
4. **Add config validation** — validate string configs (hex colors) on registration
5. **Conditional debug logs** — wrap debug logging in `#ifdef DEBUG`

### Long-term (v1.0.0)

6. **Input sanitization** — validate window pointers before use
7. **Fuzzing** — test config parsing with malformed inputs

---

## 8. Security Score

| Dimension | Score | Notes |
|-----------|-------|-------|
| Memory Safety | 9/10 | No raw pointers, proper weak_ptr usage |
| Concurrency | 9/10 | Single-threaded, no races |
| Config Security | 6/10 | Partial validation, no bounds checking |
| Render-Path Safety | 7/10 | Dead code latent risk |
| Secrets | 10/10 | No hardcoded secrets |
| Dependencies | 9/10 | All system packages |
| **Overall** | **8.3/10** | Good foundation, needs validation |

---

*Report generated by Security Audit agent*
