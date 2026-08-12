#!/bin/sh
# Build a distributable Knob.dmg. Usage: packaging/make-dmg.sh ["Codesign Identity"]
# For public distribution, sign with Developer ID and notarize afterwards:
#   xcrun notarytool submit Knob.dmg --keychain-profile <profile> --wait
#   xcrun stapler staple Knob.dmg
set -e
cd "$(dirname "$0")/.."

IDENTITY="${1:-}"
CMAKE_ARGS="-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DCMAKE_BUILD_TYPE=Release"
if [ -n "$IDENTITY" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DKNOB_CODESIGN_IDENTITY=$IDENTITY"
fi

cmake -B build-release -G Ninja $CMAKE_ARGS
cmake --build build-release

# Bundle Qt frameworks so the app runs on machines without Homebrew Qt.
/opt/homebrew/opt/qt/bin/macdeployqt build-release/KnobApp.app -always-overwrite
if [ -n "$IDENTITY" ]; then
    codesign --force --deep --sign "$IDENTITY" build-release/KnobApp.app
fi

STAGE=$(mktemp -d)
cp -R build-release/KnobApp.app "$STAGE/Knob.app"
ln -s /Applications "$STAGE/Applications"
rm -f Knob.dmg
hdiutil create -volname "Knob" -srcfolder "$STAGE" -ov -format UDZO Knob.dmg
rm -rf "$STAGE"
echo "Built Knob.dmg"
