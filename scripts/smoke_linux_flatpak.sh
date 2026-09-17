#!/usr/bin/env bash
set -euo pipefail
bundle=$(realpath "${1:?usage: smoke_linux_flatpak.sh package.flatpak}")
app=io.github.alisontristao.TraceView
flatpak install --user --noninteractive -y "$bundle"
stage=$(mktemp -d)
trap 'flatpak kill "$app" 2>/dev/null || true; rm -rf -- "$stage"' EXIT
flatpak info --user --show-permissions "$app"
set +e
timeout 20 flatpak run --user --env=QT_QPA_PLATFORM=offscreen "$app" >"$stage/startup.log" 2>&1
status=$?
set -e
cat "$stage/startup.log"
if [[ $status != 124 ]]; then
    echo "Expected TraceView to stay running; exit status: $status" >&2
    exit 1
fi
if grep -Ei 'cannot load library|could not load.*plugin|error while loading shared libraries|TLS initialization failed|does not support TLS' "$stage/startup.log"; then
    exit 1
fi
echo 'Flatpak smoke passed (startup and TLS backend; hardware requires manual testing)'
