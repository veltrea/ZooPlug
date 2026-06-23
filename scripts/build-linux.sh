#!/usr/bin/env bash
# Build the ZooPlug plug-in (.fmx) and run the unit test on Linux.
# Uses the bundled libFMWrapper.so. Claris builds the official sample with
# clang + libc++, so prefer that toolchain when it is available (ABI safety).
#
# Usage:
#   bash scripts/build-linux.sh                # ZooPlug.fmx (zoo_* + extensions)
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET_NAME="ZooPlug"
BUILD_DIR="build"
VARIANT_ARGS=()

echo "Variant: $TARGET_NAME  (build dir: $BUILD_DIR)"

CMAKE_ARGS=(-DBUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release "${VARIANT_ARGS[@]}")
if command -v clang++ >/dev/null 2>&1 && \
   printf '#include <version>\nint main(){}\n' | clang++ -stdlib=libc++ -x c++ - -o /dev/null >/dev/null 2>&1; then
    echo "Using clang++ with libc++"
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-stdlib=libc++)
else
    echo "clang++/libc++ not usable; falling back to the default compiler"
fi

rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j

echo "=== unit test ==="
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "=== exported entry point ==="
{ nm -D "$BUILD_DIR/${TARGET_NAME}.fmx" 2>/dev/null || objdump -T "$BUILD_DIR/${TARGET_NAME}.fmx" 2>/dev/null; } | grep -i FMExternCallProc || true
file "$BUILD_DIR/${TARGET_NAME}.fmx"
