#!/usr/bin/env bash
set -euo pipefail

# Usage: bash scripts/build_linux_appimage.sh [configured-release-build]
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    echo 'AppImage packaging requires Linux x86_64.' >&2; exit 1;
}
build=$(realpath "${1:-build/linux-ninja-release}")
version=$(cat "$build/appimage-version.txt")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo 'Invalid or missing CMake project version.' >&2; exit 1;
}
tools="$build/appimage-tools"
mkdir -p "$tools"
fetch_tool() {
    local name=$1 repo=$2
    if [[ ! -x "$tools/$name" ]]; then
        curl --fail --location --retry 3 \
            "https://github.com/linuxdeploy/$repo/releases/download/continuous/$name" \
            -o "$tools/$name.download"
        chmod +x "$tools/$name.download"
        mv "$tools/$name.download" "$tools/$name"
    fi
}
if [[ -z ${LINUXDEPLOY:-} ]]; then
    fetch_tool linuxdeploy-x86_64.AppImage linuxdeploy
    LINUXDEPLOY="$tools/linuxdeploy-x86_64.AppImage"
fi
if [[ -z ${LINUXDEPLOY_PLUGIN_QT:-} ]]; then
    fetch_tool linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-qt
    LINUXDEPLOY_PLUGIN_QT="$tools/linuxdeploy-plugin-qt-x86_64.AppImage"
fi
LINUXDEPLOY=$(realpath "$LINUXDEPLOY")
LINUXDEPLOY_PLUGIN_QT=$(realpath "$LINUXDEPLOY_PLUGIN_QT")
# A fresh AppDir prevents stale libraries entering a new build.
stage=$(mktemp -d "${TMPDIR:-/tmp}/traceview-appimage.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
mkdir "$stage/tools"
# Normalize a supplied plugin filename for linuxdeploy's plugin discovery.
ln -s "$LINUXDEPLOY_PLUGIN_QT" "$stage/tools/linuxdeploy-plugin-qt"
export PATH="$stage/tools:$tools:$PATH"
export QMAKE="${QMAKE:-$(command -v qmake6)}"
export APPIMAGE_EXTRACT_AND_RUN=1
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqoffscreen.so;libqwayland-egl.so;libqwayland-generic.so}"
export VERSION="$version" ARCH=x86_64

cmake --install "$build" --prefix "$stage/AppDir/usr"
# Let linuxdeploy's AppRun keep its Qt environment setup, then apply our
# platform default before launching TraceView. Explicit user choices win.
mkdir -p "$stage/AppDir/apprun-hooks"
cat > "$stage/AppDir/apprun-hooks/traceview-platform.sh" <<'EOF'
#!/usr/bin/env bash
# XCB is currently more stable for TraceView, including under XWayland.
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    export QT_QPA_PLATFORM=xcb
fi
EOF
cd "$stage"
export OUTPUT="TraceView-$version-linux-x64.AppImage"
# Qt's TLS backend dlopens OpenSSL, so ldd cannot discover libssl for us.
openssl=$(ldconfig -p | awk '$1 == "libssl.so.3" { if (!path) path=$NF } END { print path }')
[[ -n $openssl ]] || { echo 'OpenSSL 3 runtime (libssl.so.3) is required.' >&2; exit 1; }
"$LINUXDEPLOY" --appdir "$stage/AppDir" --library "$openssl" --plugin qt --output appimage
test -s "$OUTPUT"
chmod +x "$OUTPUT"
mv -- "$OUTPUT" "$build/$OUTPUT"
echo "Created $build/$OUTPUT"
