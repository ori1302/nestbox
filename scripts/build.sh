#!/bin/sh
# Builds everything on a Mac into $NESTBOX_BUILD_DIR (default build/): the simulator, the nestbox command, the tools
# and tests (CMake), then the menu bar app and its zip, the file the device's USB drive carries.
set -e
cd "$(dirname "$0")/.."
OUT="${NESTBOX_BUILD_DIR:-build}"
cmake -S . -B "$OUT" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$OUT" -j
NESTBOX_BUILD_DIR="$OUT" scripts/build_app.sh > /dev/null && echo "built: $OUT/NestBox.app"
NESTBOX_BUILD_DIR="$OUT" scripts/package_app.sh
echo "run $OUT/nestbox-sim, or open $OUT/NestBox.app"
