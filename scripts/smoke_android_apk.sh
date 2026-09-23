#!/usr/bin/env bash
# Installs the release APK on an already-booted emulator/device (adb must see
# exactly one), launches it and checks it is still alive after 15 s with no
# crash in logcat. Used by release.yml's android-smoke job, which runs it on
# an x86_64 API 30 emulator: the APK is arm64-only, and those system images
# run arm64 apps through Android's built-in ARM translation.
set -euo pipefail
apk=$(realpath "${1:?usage: smoke_android_apk.sh package.apk}")
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:?ANDROID_HOME or ANDROID_SDK_ROOT must be set}}
aapt=$(ls -d "$sdk"/build-tools/*/aapt | sort -V | tail -n1)

package=$("$aapt" dump badging "$apk" | sed -n "s/^package: name='\([^']*\)'.*/\1/p")
activity=$("$aapt" dump badging "$apk" | sed -n "s/^launchable-activity: name='\([^']*\)'.*/\1/p")
[[ -n $package && -n $activity ]] || { echo 'Could not read package/activity from the APK' >&2; exit 1; }
echo "Package: $package  Activity: $activity"

adb install -r "$apk"
adb logcat -c
adb shell am start -W -n "$package/$activity"
sleep 15
adb logcat -d > "${RUNNER_TEMP:-/tmp}/android-smoke-logcat.txt"

failed=0
if ! adb shell pidof "$package" >/dev/null; then
    echo "$package is not running 15 s after launch" >&2
    failed=1
fi
if grep -E "FATAL EXCEPTION|Fatal signal|ANR in $package|UnsatisfiedLinkError|dlopen failed" \
        "${RUNNER_TEMP:-/tmp}/android-smoke-logcat.txt"; then
    echo 'Crash markers found in logcat' >&2
    failed=1
fi
if [[ $failed != 0 ]]; then
    echo '--- logcat (tail) ---'
    tail -n 300 "${RUNNER_TEMP:-/tmp}/android-smoke-logcat.txt"
    exit 1
fi

echo "Android APK smoke passed (install and startup)"
