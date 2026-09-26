#!/usr/bin/env bash
# Installs the release APK on an already-booted emulator/device (adb must see
# exactly one), launches it and checks it is still alive after 15 s with no
# crash of its own. Used by release.yml's android-smoke job, which runs it on
# an x86_64 API 34 emulator: the APK is arm64-only, and those system images
# run arm64 apps through Android's built-in ARM translation.
#
# Only TraceView's own crashes count: a freshly booted emulator's system
# apps (Play Services especially) log FATAL EXCEPTIONs of their own, so the
# whole-device logcat is saved for reference but never grepped for failure.
set -euo pipefail
apk=$(realpath "${1:?usage: smoke_android_apk.sh package.apk}")
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:?ANDROID_HOME or ANDROID_SDK_ROOT must be set}}
aapt=$(ls -d "$sdk"/build-tools/*/aapt | sort -V | tail -n1)
out=${RUNNER_TEMP:-/tmp}/android-smoke
mkdir -p "$out"

package=$("$aapt" dump badging "$apk" | sed -n "s/^package: name='\([^']*\)'.*/\1/p")
activity=$("$aapt" dump badging "$apk" | sed -n "s/^launchable-activity: name='\([^']*\)'.*/\1/p")
[[ -n $package && -n $activity ]] || { echo 'Could not read package/activity from the APK' >&2; exit 1; }
echo "Package: $package  Activity: $activity"
echo "Device ABIs: $(adb shell getprop ro.product.cpu.abilist)"

adb install -r "$apk"
adb logcat -c
adb logcat -b crash -c || true
# Right after install the system is still reconciling the new package's data
# directories, and a launch in that window can fail inside system_server
# ("ActivityManager: Failure starting process <package>") before any of the
# app's code runs. That is the emulator, not TraceView, so relaunch when that
# exact message shows up; a crash of the app itself is never retried.
for attempt in 1 2 3; do
    adb shell am start -W -n "$package/$activity"
    sleep 3
    adb logcat -d | grep -q "Failure starting process $package" || break
    (( attempt < 3 )) || break
    echo "Launch attempt $attempt failed in system_server before the app ran; retrying"
    adb logcat -c
    sleep 5
done
sleep 12

adb logcat -d > "$out/logcat-full.txt"
adb logcat -b crash -d > "$out/logcat-crash.txt" || true
# Lines about the app from any tag: its own output, ActivityManager's
# start/death notices, the native crash dump (tag DEBUG), linker errors.
grep -E "$package|libTraceView|DEBUG +:|linker|AndroidRuntime|libc +:" \
    "$out/logcat-full.txt" > "$out/logcat-app.txt" || true

reasons=()
pid=$(adb shell pidof "$package" | tr -d '\r' || true)
[[ -n $pid ]] || reasons+=("$package is not running 15 s after launch")
grep -q "$package" "$out/logcat-crash.txt" && reasons+=("crash buffer has a crash for $package")
grep -qE "ANR in $package" "$out/logcat-full.txt" && reasons+=("$package hit an ANR")

if (( ${#reasons[@]} )); then
    printf 'FAILED: %s\n' "${reasons[@]}" >&2
    echo '--- crash buffer ---'
    cat "$out/logcat-crash.txt"
    echo '--- app-related logcat (last 200 lines) ---'
    tail -n 200 "$out/logcat-app.txt"
    printf 'FAILED: %s\n' "${reasons[@]}" >&2
    exit 1
fi

echo "Android APK smoke passed (install and startup, pid $pid)"
