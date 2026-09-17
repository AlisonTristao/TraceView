#!/usr/bin/env bash
set -euo pipefail
image=$(realpath "${1:?usage: smoke_linux_appimage.sh package.AppImage}")
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
cd "$stage"
cp -- "$image" ./package.AppImage
chmod +x ./package.AppImage
./package.AppImage --appimage-extract >/dev/null
export QT_QPA_PLATFORM=offscreen
export XDG_CONFIG_HOME="$stage/config" XDG_DATA_HOME="$stage/data"
export XDG_CACHE_HOME="$stage/cache"
set +e
timeout 15 ./squashfs-root/AppRun >startup.log 2>&1
status=$?
set -e
cat startup.log
if [[ $status != 124 ]]; then
    echo "Expected TraceView to stay running; exit status: $status" >&2
    exit 1
fi
if grep -Ei 'cannot load library|could not load.*plugin|error while loading shared libraries|TLS initialization failed|does not support TLS' startup.log; then
    exit 1
fi

echo "AppImage smoke passed (startup and TLS backend)"
