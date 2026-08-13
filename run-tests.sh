#!/bin/bash
# focusZ Test Runner
# Runs all tests for the focusZ Hyprland plugin

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-tests"

echo "🧪 focusZ Test Runner"
echo "===================="
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "📦 Building tests..."
    cmake -B "$BUILD_DIR" -DBUILD_TESTS=ON
    cmake --build "$BUILD_DIR" -j$(nproc)
    echo ""
fi

# Run unit tests
echo "🔬 Running unit tests..."
cd "$BUILD_DIR"
if ./tests/focusZ-tests; then
    echo ""
    echo "✅ All unit tests passed!"
else
    echo ""
    echo "❌ Some unit tests failed!"
    exit 1
fi

echo ""
echo "📊 Test Summary"
echo "=============="
echo "Test binary: $BUILD_DIR/tests/focusZ-tests"
echo "Test count: $(grep -c "TEST_F\|TEST(" "$SCRIPT_DIR/tests/test_depth_focus.cpp")"
echo ""
echo "To run tests manually:"
echo "  cd $BUILD_DIR && ctest --output-on-failure"
echo ""
echo "To run specific test:"
echo "  $BUILD_DIR/tests/focusZ-tests --gtest_filter='RandSeedTest.*'"
