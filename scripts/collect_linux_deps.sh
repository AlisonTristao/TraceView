#!/usr/bin/env bash
# Turns the plain `bin/TraceView` install(TARGETS) output into a self-contained
# package: bundles Qt (and Qt's own third-party shared libs -- ICU, OpenSSL,
# fontconfig/freetype, etc.) plus the Qt plugin categories the app's linked
# modules (Widgets, SerialPort, Network) need at runtime, so the target
# machine no longer needs a matching system Qt6 install. This is the Linux
# counterpart to windeployqt + scripts/collect_windows_deps.sh's ldd sweep --
# see the CMakeLists.txt CPack section for how the two are wired up the same
# way (bundle everything except what's safe to assume the host already has).
#
# What's excluded from bundling (left to resolve against the host's own
# copy): glibc itself and the X11/Wayland/GL/driver stack. Those are the
# Linux equivalent of Windows' kernel32/user32/gdi32 -- tied to the kernel
# ABI and the display server/GPU driver on the target machine, so bundling
# them does not help portability, it just risks a mismatch. Everything else
# Qt pulls in (compression, fonts, unicode, TLS) gets bundled, same as
# windeployqt does on Windows.
#
# Usage: collect_linux_deps.sh <staged-install-prefix> <qt-plugins-source-dir>
# No-ops (with a warning, exit 0) if `patchelf` isn't on PATH -- the package
# would still work on a machine with a matching system Qt6, same as before
# this script existed, just not on one without it.
set -euo pipefail

prefix="${1:?usage: collect_linux_deps.sh <prefix> <qt-plugins-dir>}"
qt_plugins_src="${2:?usage: collect_linux_deps.sh <prefix> <qt-plugins-dir>}"

if ! command -v patchelf >/dev/null 2>&1; then
    echo "collect_linux_deps.sh: patchelf not found on PATH, skipping -- the" >&2
    echo "package will still need a matching system Qt6 install to run." >&2
    exit 0
fi

bin_dir="$prefix/bin"
lib_dir="$prefix/lib"
plugins_dir="$prefix/plugins"
mkdir -p "$lib_dir" "$plugins_dir"

# Everything below this is assumed already present, correctly versioned, on
# any machine the package targets -- glibc/libgcc (kernel & C++ ABI), and the
# X11/Wayland/GL/driver stack (must match the running display server/GPU
# driver, bundling a copy would only risk a conflict with it). The xcb branch
# only lists the actual X11/xcb protocol libraries (part of any X11 client
# stack) -- it deliberately does NOT blanket-match every libxcb-*, because
# libxcb-icccm/-image/-keysyms/-render-util are the separate "xcb-util"
# convenience project Qt's own xcb plugin links against, not the X protocol
# itself: found missing (Qt platform plugin failing to load with "libxcb-
# icccm.so.4: cannot open shared object file") on a clean Fedora, which
# doesn't pull them in the way Ubuntu's qt6-base-dev does on the build
# machine -- so those four fall through below and get bundled like any other
# Qt-side dependency instead of being assumed present on the target. Same
# reasoning excludes "selinux" from the glibc-family group below it: it's
# not part of glibc itself, just something the Ubuntu build machine's
# libmount/libgio/etc. happen to be linked against -- Arch doesn't ship
# libselinux at all (SELinux isn't part of its base system, and there isn't
# even a distro package for it to tell an Arch user to install), so it has
# to be bundled too, found the same way (a clean archlinux/base container
# with no SELinux support whatsoever failing every one of those with
# "libselinux.so.1: cannot open shared object file").
system_lib_regex='^lib(c|m|dl|rt|pthread|resolv|util|nss_[a-z]+)\.so|^ld-linux|^linux-vdso\.so|^libgcc_s\.so|^lib(X11(-xcb)?|xcb|xcb-(shm|sync|xfixes|randr|render|shape|present|dri2|dri3|glx|composite|damage|xkb|res|screensaver|record|xtest|xv|xvmc|dpms|dbe)|Xau|Xdmcp|Xext|Xrender|Xi|Xrandr|Xfixes|Xcursor|Xcomposite|Xdamage|Xtst)\.so|^lib(GL|GLX|GLdispatch|EGL|gbm|drm)\.so|^libwayland-|^libxkbcommon'

copy_deps() {
    local target="$1"
    ldd "$target" 2>/dev/null | sed -nE 's/^[^=]+=> ([^ ]+) \(0x[0-9a-fA-F]+\)$/\1/p'
}

# Fixed-point sweep: a freshly-copied lib can itself need another one not yet
# present (same reasoning as collect_windows_deps.sh), so keep passing over
# bin/ + lib/ + plugins/ until a full pass copies nothing new.
changed=1
while [ "$changed" -eq 1 ]; do
    changed=0
    while IFS= read -r -d '' elf; do
        while IFS= read -r depPath; do
            [ -n "$depPath" ] || continue
            depName="$(basename "$depPath")"
            if echo "$depName" | grep -qE "$system_lib_regex"; then
                continue
            fi
            if [ ! -e "$lib_dir/$depName" ]; then
                echo "collect_linux_deps.sh: bundling $depName (needed by $(basename "$elf"))"
                cp -L "$depPath" "$lib_dir/"
                changed=1
            fi
        done < <(copy_deps "$elf")
    done < <(find "$bin_dir" "$lib_dir" "$plugins_dir" -type f \( -name '*.so*' -o -perm -u+x \) -print0 2>/dev/null)
done

# Only the plugin categories the app's linked Qt modules (Widgets, SerialPort,
# Network) can actually dlopen at runtime -- not designer/qmllint/sqldrivers/
# printsupport/etc., which nothing here loads. The three wayland-* ones are
# needed alongside platforms/ itself: bundling libqwayland-egl.so without
# them gets the window created but with "No shell integration" / "Failed to
# load client buffer integration" logged and no working window decorations,
# found by actually launching the packaged binary under Wayland (WSLg),
# not just an `ldd` pass -- ldd can't see plugin-to-plugin dlopen calls.
for category in platforms tls imageformats networkinformation \
    wayland-shell-integration wayland-graphics-integration-client \
    wayland-decoration-client; do
    src="$qt_plugins_src/$category"
    if [ -d "$src" ]; then
        mkdir -p "$plugins_dir/$category"
        cp -L "$src"/*.so "$plugins_dir/$category/"
    fi
done

# Plugins can need bundled libs too (e.g. tls's OpenSSL backend) -- sweep
# again now that they're present.
changed=1
while [ "$changed" -eq 1 ]; do
    changed=0
    while IFS= read -r -d '' elf; do
        while IFS= read -r depPath; do
            [ -n "$depPath" ] || continue
            depName="$(basename "$depPath")"
            if echo "$depName" | grep -qE "$system_lib_regex"; then
                continue
            fi
            if [ ! -e "$lib_dir/$depName" ]; then
                echo "collect_linux_deps.sh: bundling $depName (needed by $(basename "$elf"))"
                cp -L "$depPath" "$lib_dir/"
                changed=1
            fi
        done < <(copy_deps "$elf")
    done < <(find "$bin_dir" "$lib_dir" "$plugins_dir" -type f \( -name '*.so*' -o -perm -u+x \) -print0 2>/dev/null)
done

# RPATH so the loader finds bundled libs without LD_LIBRARY_PATH: the binary
# and plugins point at lib/ (one and two levels up respectively), and every
# bundled lib points at its own directory so lib-to-lib deps resolve too.
patchelf --set-rpath '$ORIGIN/../lib' "$bin_dir/TraceView"
find "$lib_dir" -maxdepth 1 -name '*.so*' -print0 | while IFS= read -r -d '' f; do
    patchelf --set-rpath '$ORIGIN' "$f"
done
find "$plugins_dir" -mindepth 2 -name '*.so' -print0 | while IFS= read -r -d '' f; do
    patchelf --set-rpath '$ORIGIN/../../lib' "$f"
done

# Qt looks for qt.conf next to the executable to relocate its "installed"
# paths -- without it, QPluginLoader falls back to the compiled-in (build
# machine's) plugin path and never finds plugins/ above.
cat > "$bin_dir/qt.conf" <<'EOF'
[Paths]
Prefix = ..
EOF
