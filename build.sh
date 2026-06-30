#!/bin/bash
# P2P Chat - Cross-platform build script
# Usage: ./build.sh [debug|release] [--voice]

set -e

BUILD_TYPE="Release"
ENABLE_VOICE="OFF"

# Parse arguments
for arg in "$@"; do
    case $arg in
        debug)
            BUILD_TYPE="Debug"
            ;;
        release)
            BUILD_TYPE="Release"
            ;;
        --voice)
            ENABLE_VOICE="ON"
            ;;
    esac
done

echo "==================================="
echo "  P2P Chat Build System"
echo "  Build Type: $BUILD_TYPE"
echo "  Voice Support: $ENABLE_VOICE"
echo "==================================="

# Create build directory
BUILD_DIR="build/${BUILD_TYPE,,}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
cmake ../.. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DENABLE_VOICE="$ENABLE_VOICE" \
    -DBUILD_TESTS=ON

# Build
cmake --build . --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "Build complete! Binaries are in: $BUILD_DIR"
echo "  - p2pchat (client)"
echo "  - signaling_server (server)"
