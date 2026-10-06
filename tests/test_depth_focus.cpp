#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================
// Test the pure functions from DepthFocus.cpp
// These functions are safe to test without Hyprland dependencies
// ============================================================

// Copied from DepthFocus.cpp to test in isolation
static uint64_t randSeed(uintptr_t seed) {
    uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return h;
}

// Copied from DepthFocus.cpp
struct CBox {
    double x, y, w, h;
};

// Copied from DepthFocus.cpp
static CBox clampToWorkarea(const CBox& target, double workX, double workY, double workW, double workH) {
    CBox clamped = target;
    if (clamped.x < workX)
        clamped.x = workX;
    if (clamped.y < workY)
        clamped.y = workY;
    if (clamped.x + clamped.w > workX + workW)
        clamped.x = workX + workW - clamped.w;
    if (clamped.y + clamped.h > workY + workH)
        clamped.y = workY + workH - clamped.h;
    return clamped;
}

// ============================================================
// randSeed tests
// ============================================================
class RandSeedTest : public ::testing::Test {};

TEST_F(RandSeedTest, DeterministicOutput) {
    // Same seed should produce same output
    uintptr_t seed = 12345;
    uint64_t result1 = randSeed(seed);
    uint64_t result2 = randSeed(seed);
    EXPECT_EQ(result1, result2);
}

TEST_F(RandSeedTest, DifferentSeedsDifferentOutputs) {
    // Different seeds should produce different outputs (with high probability)
    uint64_t result1 = randSeed(1);
    uint64_t result2 = randSeed(2);
    EXPECT_NE(result1, result2);
}

TEST_F(RandSeedTest, Distribution) {
    // Check that outputs are reasonably distributed across the 64-bit range
    const int NUM_SAMPLES = 1000;
    std::vector<uint64_t> outputs;
    outputs.reserve(NUM_SAMPLES);
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        outputs.push_back(randSeed(i));
    }
    
    // Check that we have variety in the high bits
    int highBitVariety = 0;
    for (int bit = 48; bit < 64; bit++) {
        bool hasZero = false, hasOne = false;
        for (auto v : outputs) {
            if (v & (1ULL << bit)) hasOne = true;
            else hasZero = true;
        }
        if (hasZero && hasOne) highBitVariety++;
    }
    
    // At least some high bits should vary
    EXPECT_GT(highBitVariety, 5);
}

TEST_F(RandSeedTest, ZeroSeed) {
    // Zero seed produces zero (mathematical property: 0 * constant = 0)
    uint64_t result = randSeed(0);
    EXPECT_EQ(result, 0u);
}

TEST_F(RandSeedTest, LargeSeed) {
    // Large seed should work correctly
    uint64_t result = randSeed(UINTPTR_MAX);
    EXPECT_NE(result, 0u);
}

// ============================================================
// clampToWorkarea tests
// ============================================================
class ClampToWorkareaTest : public ::testing::Test {
protected:
    // Standard workarea: 1920x1080 at (0,0)
    const double WORK_X = 0;
    const double WORK_Y = 0;
    const double WORK_W = 1920;
    const double WORK_H = 1080;
};

TEST_F(ClampToWorkareaTest, InsideWorkarea) {
    CBox card = {100, 100, 400, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    EXPECT_DOUBLE_EQ(result.x, 100);
    EXPECT_DOUBLE_EQ(result.y, 100);
    EXPECT_DOUBLE_EQ(result.w, 400);
    EXPECT_DOUBLE_EQ(result.h, 300);
}

TEST_F(ClampToWorkareaTest, LeftEdge) {
    CBox card = {-50, 100, 400, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    EXPECT_DOUBLE_EQ(result.x, 0);
    EXPECT_DOUBLE_EQ(result.y, 100);
}

TEST_F(ClampToWorkareaTest, TopEdge) {
    CBox card = {100, -50, 400, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    EXPECT_DOUBLE_EQ(result.x, 100);
    EXPECT_DOUBLE_EQ(result.y, 0);
}

TEST_F(ClampToWorkareaTest, RightEdge) {
    CBox card = {1800, 100, 400, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    // Should clamp to keep card within workarea
    EXPECT_LE(result.x + result.w, WORK_W);
}

TEST_F(ClampToWorkareaTest, BottomEdge) {
    CBox card = {100, 900, 400, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    // Should clamp to keep card within workarea
    EXPECT_LE(result.y + result.h, WORK_H);
}

TEST_F(ClampToWorkareaTest, CardLargerThanWorkarea) {
    // Card wider than workarea should be clamped to left edge
    // Note: clampToWorkarea doesn't preserve width, it just clamps position
    CBox card = {100, 100, 2000, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    // Position is clamped, but width is preserved
    EXPECT_DOUBLE_EQ(result.x, -80);  // Clamped: 1920 - 2000 = -80
    EXPECT_DOUBLE_EQ(result.w, 2000);
}

TEST_F(ClampToWorkareaTest, CenteredCard) {
    // Card centered in workarea should stay centered
    CBox card = {760, 390, 400, 300};
    CBox result = clampToWorkarea(card, WORK_X, WORK_Y, WORK_W, WORK_H);
    
    EXPECT_DOUBLE_EQ(result.x, 760);
    EXPECT_DOUBLE_EQ(result.y, 390);
}

// ============================================================
// Scale calculation tests (from getTransformForLayer)
// ============================================================
class ScaleCalculationTest : public ::testing::Test {
protected:
    const float LAYER1_SCALE = 0.70f;
    const float LAYER2_SCALE = 0.50f;
    const float LAYER1_OPACITY = 0.85f;
    const float LAYER2_OPACITY = 0.70f;
    const float FLOOR_SCALE = 0.22f;
    const float FLOOR_OPACITY = 0.05f;
    const int MAX_LAYERS = 8;
    
    struct LayerTransform {
        float scale;
        float opacity;
    };
    
    LayerTransform getTransformForLayer(int depth) const {
        LayerTransform t;
        if (depth <= 0) {
            t.scale = 1.0f;
            t.opacity = 1.0f;
        } else if (depth == 1) {
            t.scale = LAYER1_SCALE;
            t.opacity = LAYER1_OPACITY;
        } else if (depth == 2) {
            t.scale = LAYER2_SCALE;
            t.opacity = LAYER2_OPACITY;
        } else {
            float baseScale = LAYER2_SCALE;
            float baseOpacity = LAYER2_OPACITY;
            int extraDepth = depth - 2;
            int totalSteps = std::max(1, MAX_LAYERS - 2);
            float frac = std::min(1.0f, (float)extraDepth / (float)totalSteps);
            t.scale = baseScale + (FLOOR_SCALE - baseScale) * frac;
            t.opacity = baseOpacity + (FLOOR_OPACITY - baseOpacity) * frac;
        }
        return t;
    }
};

TEST_F(ScaleCalculationTest, Layer0FullScale) {
    auto t = getTransformForLayer(0);
    EXPECT_FLOAT_EQ(t.scale, 1.0f);
    EXPECT_FLOAT_EQ(t.opacity, 1.0f);
}

TEST_F(ScaleCalculationTest, Layer1Scale) {
    auto t = getTransformForLayer(1);
    EXPECT_FLOAT_EQ(t.scale, LAYER1_SCALE);
    EXPECT_FLOAT_EQ(t.opacity, LAYER1_OPACITY);
}

TEST_F(ScaleCalculationTest, Layer2Scale) {
    auto t = getTransformForLayer(2);
    EXPECT_FLOAT_EQ(t.scale, LAYER2_SCALE);
    EXPECT_FLOAT_EQ(t.opacity, LAYER2_OPACITY);
}

TEST_F(ScaleCalculationTest, DeepLayerApproachesFloor) {
    // Deep layers should approach floor values
    auto t = getTransformForLayer(MAX_LAYERS);
    EXPECT_LE(t.scale, LAYER2_SCALE);
    EXPECT_GE(t.scale, FLOOR_SCALE);
    EXPECT_LE(t.opacity, LAYER2_OPACITY);
    EXPECT_GE(t.opacity, FLOOR_OPACITY);
}

TEST_F(ScaleCalculationTest, ScaleDecreasesMonotonically) {
    // Scale should decrease as depth increases
    float prevScale = 1.0f;
    for (int depth = 1; depth <= MAX_LAYERS; depth++) {
        auto t = getTransformForLayer(depth);
        EXPECT_LE(t.scale, prevScale);
        prevScale = t.scale;
    }
}

TEST_F(ScaleCalculationTest, OpacityDecreasesMonotonically) {
    // Opacity should decrease as depth increases
    float prevOpacity = 1.0f;
    for (int depth = 1; depth <= MAX_LAYERS; depth++) {
        auto t = getTransformForLayer(depth);
        EXPECT_LE(t.opacity, prevOpacity);
        prevOpacity = t.opacity;
    }
}

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
