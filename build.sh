#!/bin/bash

# Cargo-style build script for CMake
# Usage: ./build.sh          # debug build (like cargo build)
#        ./build.sh --release # release build (like cargo build --release)

set -e

BUILD_TYPE="Debug"
BUILD_DIR="build/debug"
CLEAN_BUILD=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --release|-r)
            BUILD_TYPE="Release"
            BUILD_DIR="build/release"
            shift
            ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            BUILD_DIR="build/relwithdebinfo"
            shift
            ;;
        --clean|-c)
            CLEAN_BUILD=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--release] [--relwithdebinfo] [--clean]"
            echo "  (no args)        Build in debug mode"
            echo "  --release        Build in release mode"
            echo "  --relwithdebinfo Build in release mode with debug info"
            echo "  --clean          Clean build directory before building"
            exit 0
            ;;
    esac
done

echo "Building usdcat in ${BUILD_TYPE} mode..."
echo "Build directory: ${BUILD_DIR}"

if [ "$CLEAN_BUILD" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

# Create build directory if it doesn't exist
mkdir -p "${BUILD_DIR}"

# Configure and build
cd "${BUILD_DIR}"
cmake -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" ../..
cmake --build . -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "✓ Build complete: ${BUILD_DIR}/usdcat"
