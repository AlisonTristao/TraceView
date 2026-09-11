#!/usr/bin/env bash
# Copies every DLL a just-deployed TraceView package still needs but doesn't
# have, by walking `ldd` over every .exe/.dll already in the target
# directory and copying in whatever it resolves to a path OUTSIDE Windows'
# own system directories. Repeats until a full pass copies nothing, since a
# freshly-copied DLL can itself need another one not yet present.
#
# Why this exists: windeployqt's --compiler-runtime only bundles
# libgcc/libstdc++/libwinpthread -- it has no notion of a Qt module's OWN
# third-party shared-library dependencies. A MinGW/MSYS2-built Qt6Network.dll
# links against a separate libbrotlidec DLL for HTTP decompression that the
# official Qt installer's kit doesn't need at all (statically linked or not
# used there), and windeployqt never looks for it -- the exact "libbrotlidec
# .dll não foi encontrado" a user hit running the first release.yml-built
# installer. Bash (not Python, unlike this directory's other scripts): this
# runs from CMakeLists.txt's install(CODE ...) step, i.e. inside whatever
# shell packaged the app (MSYS2 in CI, git-bash locally) -- `ldd` is already
# guaranteed there and nowhere else this project runs.
#
# Usage: collect_windows_deps.sh <directory-to-fix-up>
# No-ops (with a warning, exit 0) if `ldd` isn't on PATH.
set -euo pipefail

dir="${1:?usage: collect_windows_deps.sh <directory>}"

if ! command -v ldd >/dev/null 2>&1; then
    echo "collect_windows_deps.sh: ldd not found on PATH, skipping" >&2
    exit 0
fi

changed=1
while [ "$changed" -eq 1 ]; do
    changed=0
    for bin in "$dir"/*.exe "$dir"/*.dll; do
        [ -e "$bin" ] || continue
        while IFS= read -r depPath; do
            [ -n "$depPath" ] || continue
            case "$depPath" in
                /[Cc]/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/*)
                    continue
                    ;;
            esac
            depName="$(basename "$depPath")"
            if [ ! -e "$dir/$depName" ]; then
                echo "collect_windows_deps.sh: copying $depName (needed by $(basename "$bin"))"
                cp "$depPath" "$dir/"
                changed=1
            fi
        done < <(ldd "$bin" 2>/dev/null | sed -nE 's/^[^=]+=> ([^ ]+) \(0x[0-9a-fA-F]+\)$/\1/p' || true)
    done
done
