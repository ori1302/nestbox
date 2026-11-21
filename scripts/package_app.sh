#!/bin/sh
# Packages the menu bar app for a fresh Mac as $NESTBOX_BUILD_DIR/NestBox.zip (default build/), the file the pet's
# USB drive carries. Unzipped and opened, it needs only the Local Network permission macOS asks for.
set -e
cd "$(dirname "$0")/.."
OUT="${NESTBOX_BUILD_DIR:-build}"
APP="$OUT/NestBox.app"
ZIP="$OUT/NestBox.zip"
[ -d "$APP" ] || scripts/build_app.sh
codesign --verify --deep --strict "$APP"
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
# The signature must survive the round trip through the zip.
CHECK=$(mktemp -d)
trap 'rm -rf "$CHECK"' EXIT
ditto -x -k "$ZIP" "$CHECK"
codesign --verify --deep --strict "$CHECK/NestBox.app"
echo "packaged: $ZIP ($(du -h "$ZIP" | cut -f1 | tr -d ' '), $(stat -f %z "$ZIP") bytes)"
