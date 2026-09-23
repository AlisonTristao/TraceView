#!/usr/bin/env bash
# Mounts the release .dmg, copies TraceView.app out the way a user would,
# launches it and fails if it is gone again within 15 s (missing Qt
# framework/plugin, bad signature, crash at startup). Runs on a GitHub macOS
# runner, which has a logged-in GUI session, so the real cocoa platform
# plugin is exercised -- not offscreen.
#
#   scripts/smoke_macos_dmg.sh <path/to/TraceView-x.y.z-macos-arm64.dmg>
set -euo pipefail

dmg=${1:?usage: smoke_macos_dmg.sh <dmg>}
mount_point=$(mktemp -d)
install_dir=$(mktemp -d)
log="$install_dir/traceview.log"
pid=""
cleanup() {
    [[ -n $pid ]] && kill "$pid" 2>/dev/null || true
    hdiutil detach "$mount_point" -quiet 2>/dev/null || true
    rm -rf "$install_dir"
}
trap cleanup EXIT

hdiutil attach "$dmg" -nobrowse -readonly -mountpoint "$mount_point" -quiet
[[ -L $mount_point/Applications ]] || { echo "FAILED: no Applications shortcut in the image" >&2; exit 1; }
cp -R "$mount_point/TraceView.app" "$install_dir/"
app="$install_dir/TraceView.app"

codesign --verify --deep --strict "$app"
/usr/libexec/PlistBuddy -c 'Print :NSBluetoothAlwaysUsageDescription' "$app/Contents/Info.plist" >/dev/null ||
    { echo "FAILED: Info.plist has no NSBluetoothAlwaysUsageDescription" >&2; exit 1; }

"$app/Contents/MacOS/TraceView" >"$log" 2>&1 &
pid=$!
sleep 15
if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" && status=0 || status=$?
    pid=""
    echo "FAILED: TraceView exited within 15 s (status $status)" >&2
    cat "$log" >&2
    exit 1
fi
echo "TraceView still running after 15 s -- OK"
tail -n 20 "$log" || true
