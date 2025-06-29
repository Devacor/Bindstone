#!/bin/bash
# Isolated test build script - prevents .o file conflicts

if [ $# -lt 1 ]; then
    echo "Usage: $0 <test_file.cpp> [additional_args...]"
    echo "Example: $0 tests/foundry/core/test_engine.cpp"
    exit 1
fi

TEST_FILE="$1"
shift  # Remove first argument, keep rest for compiler

# Extract test name from path
TEST_NAME=$(basename "$TEST_FILE" .cpp)

# Create isolated build directory
BUILD_DIR="/tmp/jai_isolated_${USER}_$$"
mkdir -p "$BUILD_DIR"

# Get absolute paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Building isolated test: $TEST_NAME"
echo "Build directory: $BUILD_DIR"

# Compile with isolated test flag
# Note: .o file goes to temp directory, not polluting main build
g++ -std=c++20 \
    -DJAI_ISOLATED_TEST \
    -I"$PROJECT_ROOT/include" \
    -o "$BUILD_DIR/$TEST_NAME" \
    "$@" \
    "$TEST_FILE" \
    -pthread

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Running: $BUILD_DIR/$TEST_NAME"
    echo "========================================="
    "$BUILD_DIR/$TEST_NAME"
    RESULT=$?
    echo "========================================="
    
    # Cleanup
    rm -rf "$BUILD_DIR"
    
    exit $RESULT
else
    echo "Build failed!"
    rm -rf "$BUILD_DIR"
    exit 1
fi