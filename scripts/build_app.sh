#!/bin/sh
# Builds the menu bar app into $NESTBOX_BUILD_DIR/NestBox.app (default build/), with swiftc and an ad-hoc
# signature; no Xcode needed. The app carries its own copy of the companion helper, for Apple Silicon and Intel
# and macOS 13 and later, so it runs on a Mac with nothing else installed.
set -e
cd "$(dirname "$0")/.."
OUT="${NESTBOX_BUILD_DIR:-build}"
APP="$OUT/NestBox.app"
OBJ="$OUT/menubar-obj"
MIN=13.0
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Helpers" "$APP/Contents/Resources" "$OBJ"
for arch in arm64 x86_64; do
  swiftc -O -swift-version 6 -target "$arch-apple-macos$MIN" \
    -framework AppKit -framework ServiceManagement -framework IOKit -framework CoreWLAN \
    companion/mac/app/*.swift -o "$OBJ/NestBox-$arch"
done
lipo -create "$OBJ/NestBox-arm64" "$OBJ/NestBox-x86_64" -output "$APP/Contents/MacOS/NestBox"
# The app draws its own icon (companion/mac/app/Icon.swift); iconutil packs it.
rm -rf "$OBJ/AppIcon.iconset"
"$APP/Contents/MacOS/NestBox" --app-icon "$OBJ/AppIcon.iconset"
iconutil -c icns "$OBJ/AppIcon.iconset" -o "$APP/Contents/Resources/AppIcon.icns"
# The helper goes in Helpers, not next to the NestBox executable: macOS file names ignore case. It is built here
# rather than taken from CMake, which targets only this Mac's macOS version.
clang++ -std=c++17 -O2 -Wall -Wextra -arch arm64 -arch x86_64 -mmacosx-version-min=$MIN -Iengine \
  companion/mac/cli/*.cpp -framework ApplicationServices -o "$APP/Contents/Helpers/nestbox"
cp companion/mac/app/Info.plist "$APP/Contents/Info.plist"
# The version is set once, in CMakeLists.txt.
VERSION=$(sed -n 's/^project(nestbox VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "no project(nestbox VERSION x.y.z) in CMakeLists.txt" >&2; exit 1; }
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion $MIN" "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
codesign --force --sign - --identifier pet.nestbox.cli "$APP/Contents/Helpers/nestbox"   # inner code first
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "built: $APP   (open it, or copy it to /Applications)"
# The developer pet window gets a bundle of its own next to the build's nestbox-sim, so macOS shows it as
# "NestBox Pet" with the goose icon. Its executable is a link to the build's, so a rebuild needs no new bundle, and
# the simulator still finds pets/ next to the real file.
if [ -x "$OUT/nestbox-sim" ]; then
  PET="$OUT/NestBox Pet.app"
  rm -rf "$PET"
  mkdir -p "$PET/Contents/MacOS" "$PET/Contents/Resources"
  ln -s ../../../nestbox-sim "$PET/Contents/MacOS/nestbox-sim"
  cp simulator/Info.plist "$PET/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$PET/Contents/Info.plist"
  cp "$APP/Contents/Resources/AppIcon.icns" "$PET/Contents/Resources/AppIcon.icns"
  printf 'APPL????' > "$PET/Contents/PkgInfo"
  echo "built: $PET   (the app's pet window)"
fi
