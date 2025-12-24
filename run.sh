#!/bin/bash

# Cargo-style run script for CMake
# Usage: ./run.sh          # build and run debug (like cargo run)
#        ./run.sh --release # build and run release (like cargo run --release)

set -e

BUILD_TYPE="Debug"
BUILD_DIR="build/debug"
RELEASE_FLAG=""

# Parse arguments
RUN_ARGS=()
for arg in "$@"; do
    case $arg in
        --release|-r)
            BUILD_TYPE="Release"
            BUILD_DIR="build/release"
            RELEASE_FLAG="--release"
            shift
            ;;
        *)
            RUN_ARGS+=("$arg")
            ;;
    esac
done

# Build first
./build.sh ${RELEASE_FLAG}

echo ""
echo "Running ${BUILD_TYPE} build..."
echo "----------------------------------------"

# Run the executable with any remaining arguments
"${BUILD_DIR}/usdcat" "${RUN_ARGS[@]}"
