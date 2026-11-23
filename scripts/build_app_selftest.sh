#!/bin/sh
# Builds the menu bar app's self-test into $NESTBOX_BUILD_DIR/nestbox-menubar-selftest (default build/): the app's
# sources without its main.swift, plus companion/mac/selftest. It tests a built NestBox.app (--app) against a
# pet started for it; see https://nestbox.pet/docs/mac-app/.
set -e
cd "$(dirname "$0")/.."
OUT="${NESTBOX_BUILD_DIR:-build}"
MIN=13.0                 # as scripts/build_app.sh
mkdir -p "$OUT"
swiftc -O -swift-version 6 -target "$(uname -m)-apple-macos$MIN" \
  -framework AppKit -framework ServiceManagement -framework IOKit -framework CoreWLAN -framework EventKit \
  $(ls companion/mac/app/*.swift | grep -v '/main\.swift$') companion/mac/selftest/*.swift \
  -o "$OUT/nestbox-menubar-selftest"
echo "built: $OUT/nestbox-menubar-selftest"
