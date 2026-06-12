#!/usr/bin/env bash
# Build the ZooPlug plug-in (ZooPlug.fmx) and run the unit test on Linux.
# Uses the bundled libFMWrapper.so. Claris builds the official sample with
# clang + libc++, so prefer that toolchain when it is available (ABI safety).
set -euo pipefail
cd "$(dirname "$0")/.."

CMAKE_ARGS=(-DBUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release)
if command -v clang++ >/dev/null 2>&1 && \
   printf '#include <version>\nint main(){}\n' | clang++ -stdlib=libc++ -x c++ - -o /dev/null >/dev/null 2>&1; then
    echo "Using clang++ with libc++"
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-stdlib=libc++)
else
    echo "clang++/libc++ not usable; falling back to the default compiler"
fi

rm -rf build
cmake -B build "${CMAKE_ARGS[@]}"
cmake --build build -j

echo "=== unit test ==="
ctest --test-dir build --output-on-failure

echo "=== exported entry point ==="
{ nm -D build/ZooPlug.fmx 2>/dev/null || objdump -T build/ZooPlug.fmx 2>/dev/null; } | grep -i FMExternCallProc || true
file build/ZooPlug.fmx
