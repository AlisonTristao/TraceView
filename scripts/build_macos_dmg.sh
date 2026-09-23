#!/usr/bin/env bash
# Turns a macos-release build's TraceView.app into the release disk image,
# TraceView-<version>-macos-arm64.dmg (the name lib/updater/updatechecker.cpp
# matches by suffix).
#
#   scripts/build_macos_dmg.sh <build-dir> <version>
#
# Steps: macdeployqt copies the Qt frameworks/plugins into the bundle, the
# whole bundle is then ad-hoc signed (Apple Silicon refuses to run code with
# no signature at all, and macdeployqt's install_name_tool edits invalidate
# the linker's own ad-hoc signature), and hdiutil packs it next to an
# /Applications shortcut for drag-to-install.
#
# Ad-hoc is NOT a Developer ID signature: Gatekeeper still blocks the first
# launch of a downloaded copy (right-click > Open, or
# `xattr -dr com.apple.quarantine /Applications/TraceView.app`). Proper
# signing + notarization needs an Apple Developer account -- see
# docs/MACOS_IOS_BUILD.md.
set -euo pipefail

build_dir=${1:?usage: build_macos_dmg.sh <build-dir> <version>}
version=${2:?usage: build_macos_dmg.sh <build-dir> <version>}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid version: $version" >&2; exit 1; }

app="$build_dir/TraceView.app"
[[ -d $app ]] || { echo "No TraceView.app under $build_dir" >&2; exit 1; }

macdeployqt=${MACDEPLOYQT:-}
if [[ -z $macdeployqt ]]; then
    if [[ -n ${QT_ROOT_DIR:-} && -x $QT_ROOT_DIR/bin/macdeployqt ]]; then
        macdeployqt="$QT_ROOT_DIR/bin/macdeployqt"
    else
        macdeployqt=$(command -v macdeployqt)
    fi
fi

"$macdeployqt" "$app" -verbose=1
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"

dmg="$build_dir/TraceView-$version-macos-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "TraceView $version" -srcfolder "$staging" -ov -format UDZO "$dmg"
echo "Wrote $dmg"
