# focusZ — Test Coverage & Plan

**Agent:** Test Coverage Gaps  
**Date:** 2026-08-13  
**Scope:** Full codebase (`src/*.cpp`, `src/*.hpp`)  
**Tool:** Manual analysis (@claude-flow/cli coverage-gaps: no coverage data)

---

## Executive Summary

| Metric | Value |
|--------|-------|
| Total Functions | 18 |
| Testable (pure) | 4 (22%) |
| Testable (with state) | 3 (17%) |
| Not testable (Hyprland deps) | 11 (61%) |
| Current Tests | 0 |
| Target Coverage | 100% of testable functions |

---

## 1. Function Classification

### 1.1 Pure Functions (Fully Testable)

| Function | Lines | Complexity | Test Type |
|----------|-------|------------|-----------|
| `getLayerDepth()` | 15 | 3 | Unit |
| `getTransformForLayer()` | 20 | 4 | Unit |
| `getDeckFactor()` | 5 | 2 | Unit |
| `winStr()` | 12 | 1 | Unit |

**These functions have NO dependencies on Hyprland APIs and can be tested in isolation.**

### 1.2 State-Dependent Functions (Testable with Setup)

| Function | Lines | Complexity | Test Type |
|----------|-------|------------|-----------|
| `refreshDepthCache()` | 30 | 5 | Integration |
| `rebuildStack()` | 70 | 15 | Integration |
| `promoteWindow()` | 40 | 8 | Integration |

**These functions depend on `CDepthFocusManager` state but can be tested by setting up the state manually.**

### 1.3 Hyprland-Dependent Functions (Not Testable)

| Function | Lines | Complexity | Reason |
|----------|-------|------------|--------|
| `onWindowOpen()` | 37 | 7 | Needs `PHLWINDOW` |
| `onWindowClose()` | 32 | 6 | Needs `PHLWINDOW` |
| `onFocusChange()` | 33 | 6 | Needs `PHLWINDOW` + `eFocusReason` |
| `layoutStack()` | 206 | 31 | Needs `PHLWINDOW` + monitor |
| `applyAllDepthTransforms()` | 80 | 18 | Needs `PHLWINDOW` |
| `applyDepthToWindow()` | 100 | 12 | Needs `PHLWINDOW` + renderer |
| `restoreWindow()` | 31 | 8 | Needs `PHLWINDOW` + renderer |
| `onRenderStage()` | 5 | 1 | No-op (testable as trivial) |
| `setWindowScale()` | 10 | 1 | Dead code (testable as trivial) |

---

## 2. Test Plan

### 2.1 Unit Tests (Pure Functions)

#### `test_getLayerDepth.cpp`

```cpp
TEST(GetLayerDepth, FocusedWindowReturnsZero) {
    // Arrange: window at position 0 in zStack
    // Act: getLayerDepth(window)
    // Assert: returns 0
}

TEST(GetLayerDepth, FirstBackWindowReturnsOne) {
    // Arrange: window at position 1 in zStack
    // Act: getLayerDepth(window)
    // Assert: returns 1
}

TEST(GetLayerDepth, SecondBackWindowReturnsTwo) {
    // Arrange: window at position 2 in zStack
    // Act: getLayerDepth(window)
    // Assert: returns 2
}

TEST(GetLayerDepth, UnknownWindowReturnsMinusOne) {
    // Arrange: window not in zStack
    // Act: getLayerDepth(window)
    // Assert: returns -1
}

TEST(GetLayerDepth, CachedValueReturned) {
    // Arrange: populate depthCache
    // Act: getLayerDepth(window)
    // Assert: cache is used, not zStack scan
}
```

#### `test_getTransformForLayer.cpp`

```cpp
TEST(GetTransformForLayer, Layer0IsIdentity) {
    // Arrange: depth = 0
    // Act: getTransformForLayer(0)
    // Assert: scale=1.0, opacity=1.0
}

TEST(GetTransformForLayer, Layer1UsesConfigValues) {
    // Arrange: depth = 1, config.layer1Scale = 0.7
    // Act: getTransformForLayer(1)
    // Assert: scale = 0.7
}

TEST(GetTransformForLayer, Layer2UsesConfigValues) {
    // Arrange: depth = 2, config.layer2Scale = 0.5
    // Act: getTransformForLayer(2)
    // Assert: scale = 0.5
}

TEST(GetTransformForLayer, DeepLayerClampsToMaxDepth) {
    // Arrange: depth = 10, maxLayers = 2
    // Act: getTransformForLayer(10)
    // Assert: uses layer2 values (clamped)
}

TEST(GetTransformForLayer, NegativeDepthReturnsIdentity) {
    // Arrange: depth = -1
    // Act: getTransformForLayer(-1)
    // Assert: scale=1.0, opacity=1.0
}
```

#### `test_getDeckFactor.cpp`

```cpp
TEST(GetDeckFactor, FocusedWindowReturnsZero) {
    // Arrange: window at position 0
    // Act: getDeckFactor(window)
    // Assert: returns 0.0
}

TEST(GetDeckFactor, FirstBackWindowReturnsInterpolated) {
    // Arrange: window at position 1
    // Act: getDeckFactor(window)
    // Assert: returns value between 0 and 1
}

TEST(GetDeckFactor, MaxDepthReturnsOne) {
    // Arrange: window at max depth
    // Act: getDeckFactor(window)
    // Assert: returns 1.0
}

TEST(GetDeckFactor, LinearInterpolation) {
    // Arrange: window at depth 1, maxLayers = 2
    // Act: getDeckFactor(window)
    // Assert: returns 0.5 (1/2)
}
```

#### `test_winStr.cpp`

```cpp
TEST(WinStr, ReturnsClassName) {
    // Arrange: mock window with class "Firefox"
    // Act: winStr(window)
    // Assert: returns "Firefox"
}

TEST(WinStr, ReturnsAddressOnNull) {
    // Arrange: nullptr window
    // Act: winStr(nullptr)
    // Assert: returns "(null)"
}
```

### 2.2 Integration Tests (State-Dependent)

#### `test_refreshDepthCache.cpp`

```cpp
TEST(RefreshDepthCache, EmptyStackClearsCache) {
    // Arrange: empty zStack, populated cache
    // Act: refreshDepthCache()
    // Assert: cache is empty
}

TEST(RefreshDepthCache, AssignsMonotonicDepths) {
    // Arrange: zStack with 3 windows
    // Act: refreshDepthCache()
    // Assert: depths are 0, 1, 2 (monotonic)
}

TEST(RefreshDepthCache, PreservesFocusedAtZero) {
    // Arrange: zStack with focused window
    // Act: refreshDepthCache()
    // Assert: focused window has depth 0
}
```

#### `test_rebuildStack.cpp`

```cpp
TEST(RebuildStack, RemovesClosedWindow) {
    // Arrange: zStack with window A, B, C
    // Act: rebuildStack() after removing B
    // Assert: zStack contains A, C only
}

TEST(RebuildStack, MaintainsOrder) {
    // Arrange: zStack with ordered windows
    // Act: rebuildStack()
    // Assert: order is preserved
}

TEST(RebuildStack, HandlesEmptyStack) {
    // Arrange: empty zStack
    // Act: rebuildStack()
    // Assert: no crash, stack remains empty
}
```

### 2.3 Trivial Tests (No-Op Functions)

#### `test_onRenderStage.cpp`

```cpp
TEST(OnRenderStage, IsNoOp) {
    // Arrange: manager with state
    // Act: onRenderStage(RENDER_POST_WINDOWS)
    // Assert: no state changed, no crash
}
```

---

## 3. Test Infrastructure

### 3.1 Framework Choice

| Framework | Pros | Cons | Recommendation |
|-----------|------|------|----------------|
| Google Test | Industry standard, good C++ support | Heavy | ✅ Recommended |
| Catch2 | Header-only, easy setup | Less mature | Alternative |
| doctest | Fastest compile | Less features | Alternative |

### 3.2 Directory Structure

```
tests/
├── CMakeLists.txt
├── test_getLayerDepth.cpp
├── test_getTransformForLayer.cpp
├── test_getDeckFactor.cpp
├── test_winStr.cpp
├── test_refreshDepthCache.cpp
├── test_rebuildStack.cpp
├── test_onRenderStage.cpp
└── mocks/
    ├── MockWindow.hpp
    └── MockManager.hpp
```

### 3.3 Mocking Strategy

For Hyprland-dependent functions, create lightweight mocks:

```cpp
struct MockWindow {
    int id;
    std::string className;
    Vector2D position;
    Vector2D size;
};

struct MockManager {
    std::vector<MockWindow> zStack;
    std::unordered_map<int, int> depthCache;
    
    int getLayerDepth(MockWindow* w) {
        // Implementation for testing
    }
};
```

### 3.4 Build Integration

```cmake
# tests/CMakeLists.txt
add_executable(focusz_tests
    test_getLayerDepth.cpp
    test_getTransformForLayer.cpp
    test_getDeckFactor.cpp
    test_winStr.cpp
    test_refreshDepthCache.cpp
    test_rebuildStack.cpp
    test_onRenderStage.cpp
)

target_link_libraries(focusz_tests
    GTest::gtest_main
    focusz_lib
)

include(GoogleTest)
gtest_discover_tests(focusz_tests)
```

---

## 4. Coverage Targets

### 4.1 Function Coverage

| Category | Current | Target |
|----------|---------|--------|
| Pure functions | 0% | 100% |
| State-dependent | 0% | 80% |
| Hyprland-dependent | 0% | N/A (manual testing) |
| **Overall** | **0%** | **60%** (testable functions) |

### 4.2 Line Coverage

| File | Current | Target |
|------|---------|--------|
| `DepthFocus.cpp` | 0% | 40% (pure + state functions) |
| `main.cpp` | 0% | 20% (config registration) |
| `DebugLog.cpp` | 0% | 80% (mostly testable) |
| **Overall** | **0%** | **35%** |

### 4.3 Branch Coverage

| Function | Target |
|----------|--------|
| `getLayerDepth()` | 100% |
| `getTransformForLayer()` | 100% |
| `getDeckFactor()` | 100% |
| `refreshDepthCache()` | 80% |
| `rebuildStack()` | 80% |

---

## 5. Recommendations

### Immediate (v0.1.1)

1. **Set up Google Test** — add `tests/` directory with CMakeLists.txt
2. **Write unit tests** for `getLayerDepth()`, `getTransformForLayer()`, `getDeckFactor()`
3. **Add CI step** — run tests on every commit

### Short-term (v0.2.0)

4. **Add integration tests** for `refreshDepthCache()`, `rebuildStack()`
5. **Create mocks** for `PHLWINDOW` and `CDepthFocusManager`
6. **Add coverage reporting** — lcov + badge

### Long-term (v1.0.0)

7. **Property-based testing** — fuzz `getTransformForLayer()` with random inputs
8. **Performance benchmarks** — Google Test benchmarks for hot paths
9. **Mutation testing** — verify tests catch real bugs

---

## 6. Test Score

| Dimension | Score | Notes |
|-----------|-------|-------|
| Testable Functions | 22% | Most functions depend on Hyprland |
| Current Coverage | 0% | No tests exist |
| Test Plan Completeness | 90% | All testable functions covered |
| Mock Strategy | 70% | Basic mocks, needs refinement |
| **Overall** | **4.0/10** | Needs implementation |

---

*Report generated by Test Coverage Gaps agent*
