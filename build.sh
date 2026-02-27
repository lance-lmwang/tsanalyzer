#!/bin/bash

# TsPacer Build & Test Script
# Usage:
#   ./build.sh          - Build the project
#   ./build.sh test     - Build and run all unit tests
#   ./build.sh clean    - Clean build artifacts

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

build_project() {
    echo "Starting build..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$PROJECT_ROOT"
    make -j$(nproc)
    echo "Build successful!"
}

if [ "$1" == "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Clean complete."
    exit 0
fi

if [ "$1" == "test" ]; then
    build_project
    echo "Running tests..."
    cd "$BUILD_DIR"
    ctest --output-on-failure
    exit 0
fi

# Default action: Build
build_project
echo "------------------------------------------------"
echo "Binary: ${BUILD_DIR}/tsp"
echo "------------------------------------------------"
