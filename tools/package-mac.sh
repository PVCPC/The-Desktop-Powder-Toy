#!/usr/bin/env bash
# Builds the Mac release: The Desktop Powder Toy.app, in a zip, for the machine it runs on (arm64 on Apple Silicon).
# Run from the repo root; needs meson and ninja. Ad hoc signed: not notarised, so the first run needs "Open Anyway".
set -euo pipefail
version=$(sed -n 's/.*DESKTOP_VERSION L"\([^"]*\)".*/\1/p' src/OverlayWindows.h)
arch=$(uname -m)
build=build-mac-release
name="The Desktop Powder Toy"
out=dist-mac
export MACOSX_DEPLOYMENT_TARGET=11.0 CC=clang CXX=clang++
flags="['-ffunction-sections','-fdata-sections','-mmacosx-version-min=11.0']"
links="['-Wl,-dead_strip','-mmacosx-version-min=11.0']"
[[ -d $build ]] || meson setup "$build" -Dbuildtype=debugoptimized -Dlto=true -Dstatic=prebuilt -Db_staticpic=false -Dstrip=false \
	-Dc_args="$flags" -Dcpp_args="$flags" -Dobjcpp_args="$flags" -Dc_link_args="$links" -Dcpp_link_args="$links" -Dobjcpp_link_args="$links" \
	-Dmanifest_macos_min_ver=11.0 -Dapp_name="$name" -Dapp_id=io.github.pvcpc.desktop-powder-toy
ninja -C "$build"
rm -rf "$out"
app="$out/The-Desktop-Powder-Toy/$name.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$build/resources/Info.plist" "$app/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string "$version" "$app/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$version" "$app/Contents/Info.plist"
cp "$build/powder" "$app/Contents/MacOS/powder"
strip -x "$app/Contents/MacOS/powder"
for icon in exe cps; do
	set=$(mktemp -d)/icon_$icon.iconset
	mkdir "$set"
	cp "resources/generated_icons/icon_${icon}_16.png" "$set/icon_16x16.png"
	cp "resources/generated_icons/icon_${icon}_32.png" "$set/icon_32x32.png"
	cp "resources/generated_icons/icon_${icon}.png" "$set/icon_128x128.png"
	iconutil -c icns "$set" -o "$app/Contents/Resources/icon_$icon.icns"
done
codesign --force --sign - --timestamp=none "$app"
cp LICENSE "$out/The-Desktop-Powder-Toy/LICENSE.txt"
cat > "$out/The-Desktop-Powder-Toy/README.txt" <<EOF
The Desktop Powder Toy $version for macOS 11 or later, native on Apple Silicon (M1 and newer)
https://github.com/PVCPC/The-Desktop-Powder-Toy

Drag "$name" to Applications and open it. The first time, macOS says it can't check the app (it isn't notarised),
which only happens once: click Done, open System Settings > Privacy & Security, scroll down and click "Open Anyway".

The simulation covers your screen above the Dock: sand piles on your windows, and everything you haven't drawn on
stays see-through and clickable. TPT's own menus are along the right and bottom edges and fade when unused. The
hourglass in the menu bar has the settings, including every Fun switch; Ctrl+Option+E opens them too.

Based on The Powder Toy (https://powdertoy.co.uk), GPL 3.0: see LICENSE.txt.
EOF
(cd "$out" && ditto -c -k --keepParent The-Desktop-Powder-Toy "The-Desktop-Powder-Toy-$version-macos-$arch.zip")
echo "$out/The-Desktop-Powder-Toy-$version-macos-$arch.zip"
