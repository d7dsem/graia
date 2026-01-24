#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# === Defaults ===
BUILD_TYPE="Debug"
TARGET="test_file_reader"
WD_DIR="$PROJECT_ROOT/wd"

# Manual compiler setup
# Default gcc (if present in PATH)
CC="gcc"
CXX="g++"

CC="clang"
CXX="clang++"

# Generator
GEN="-G Ninja"

# === Parse CLI arguments ===
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t)
            TARGET="$2"
            shift 2
            ;;
        -m)
            BUILD_TYPE="$2"
            shift 2
            ;;
        *)
            echo "ERROR: Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# === Validate BUILD_TYPE ===
if [[ "$BUILD_TYPE" != "Debug" && "$BUILD_TYPE" != "Release" ]]; then
    echo "ERROR: BUILD_TYPE must be Debug or Release, got: $BUILD_TYPE" >&2
    exit 1
fi

BUILD_DIR="$PROJECT_ROOT/build_${CC}_${BUILD_TYPE}"

# === Ensure build directory ===
mkdir -p "$BUILD_DIR"

# === Configure ===
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" $GEN \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX"

# === Build target ===
cmake --build "$BUILD_DIR" --target "$TARGET"

# === Ensure wd directory ===
mkdir -p "$WD_DIR"

# === Copy built artifact ===
SRC_EXE="$BUILD_DIR/bin/$TARGET"
if [[ ! -f "$SRC_EXE" ]]; then
    echo "ERROR: Built file not found: $SRC_EXE" >&2
    exit 2
fi

echo "Copying $SRC_EXE to $WD_DIR/"
cp -f "$SRC_EXE" "$WD_DIR/"

echo "OK: $TARGET deployed to $WD_DIR/"