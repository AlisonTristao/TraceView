# Android build (T44/T45)

Status: environment chosen and documented here (T44); the CMake preset
below is an **unvalidated starting point** for T45 — nobody has configured,
built, signed or installed this on an actual Android SDK/NDK/device yet.
Treat every version number and path below as "what to try first," not as
proven-working. Update this file the moment any of it turns out wrong on a
real setup.

## Why these versions

TraceView already builds against Qt 6.9.2 for desktop (see the root
`CMakeLists.txt` and `CONTRIBUTING.md`'s Windows/Linux instructions).
Targeting the **same** Qt version for Android avoids maintaining two
divergent Qt versions across platforms, and Qt's own compatibility matrix
for 6.9 is:

| Requirement | Version |
|---|---|
| Qt for Android kit | 6.9.2 (same as desktop) |
| Android NDK | r27c (27.2.12479018) — r26b also supported by Qt 6.9; use whichever your Qt Maintenance Tool's "Android" component actually bundled/tested against, and correct this row once you know |
| JDK | 17 |
| Gradle / AGP | 8.10 / 8.6.0 (normally managed automatically by `androiddeployqt`'s generated Gradle project — only relevant if something needs overriding) |
| `minSdkVersion` | 28 (Android 9) — Qt 6.9's own floor |
| `compileSdkVersion` / `targetSdkVersion` | 35 (Android 15) — the top of Qt 6.9's tested range |
| ABI | `arm64-v8a` only (matches T45's "pacote ARM64" scope; Qt 6.9 also supports `armeabi-v7a`/`x86`/`x86_64` if a second ABI is ever needed) |

Source: [Qt for Android — Qt 6.9](https://doc.qt.io/qt-6.9/android.html) and
the [Qt 6.9 Android Updates blog post](https://www.qt.io/blog/qt-6.9-android-updates).
Re-check this table against whatever Qt version is actually installed
before relying on it — Qt's supported API range moves forward each minor
release (see the [Android 15/16 support post](https://www.qt.io/blog/android-15-and-16-support)
for what changes in later Qt versions).

`minSdkVersion` 28 (not higher) is deliberate: T46 needs to support both the
modern Bluetooth runtime-permission model (`BLUETOOTH_SCAN`/
`BLUETOOTH_CONNECT`, Android 12/API 31+) and the older
`ACCESS_FINE_LOCATION`-gated BLE scan (pre-31). No custom
`AndroidManifest.xml` exists yet -- the `android-arm64` preset below relies
on Qt's own default template for now, which is enough to get T45's "does it
install and open" milestone but declares none of these permissions. Adding
a real `android/AndroidManifest.xml` (via `ANDROID_PACKAGE_SOURCE_DIR`) with
`INTERNET`/`ACCESS_NETWORK_STATE` (TCP) and the Bluetooth permission set
above is T46's job, not this one -- write it against Qt's own
androiddeployqt template for whatever Qt version ends up installed, rather
than from scratch, since a hand-written manifest can silently break
packaging if it's missing a placeholder androiddeployqt expects to
substitute.

Transport selection mirrors the desktop TCP/BLE-only build already exercised
in T38: `TRACEVIEW_ENABLE_SERIAL=OFF` and `TRACEVIEW_ENABLE_USB_HID=OFF`
(neither QSerialPort nor hidapi's USB paths mean anything on a phone/tablet),
`TRACEVIEW_ENABLE_TCP=ON` and `TRACEVIEW_ENABLE_BLE=ON`.

## One-time environment setup

1. **Qt for Android kit**: open the Qt Maintenance Tool (or Qt Online
   Installer) → under Qt 6.9.2, select the **Android** component (installs
   an `android_arm64_v8a`-suffixed kit alongside whatever desktop kit is
   already installed) → note the exact NDK version it pulls in and fix the
   table above if it differs from r27c.
2. **Android SDK + NDK**: the Qt installer's Android component can install
   these for you (recommended — it picks a version it has actually tested
   against), or install them separately via Android Studio's SDK Manager /
   the standalone `cmdline-tools` + `sdkmanager`. Either way, install:
   - `platforms;android-35` (compile/target SDK)
   - `platform-tools`
   - `build-tools;35.0.0` (or whatever matches the AGP/Gradle pair above)
   - `ndk;27.2.12479018`
3. **JDK 17**: this machine already has a JDK under
   `C:\Program Files\Eclipse Adoptium\jdk-25.0.2.10-hotspot` — that's JDK 25,
   newer than the JDK 17 Qt 6.9 was validated against. Install a JDK 17
   alongside it (Temurin 17 is a safe choice) rather than assuming the newer
   one works; point `JAVA_HOME` at the JDK 17 install when building Android,
   not at the existing JDK 25.
4. **Environment variables** (set before configuring, e.g. in
   `CMakeUserPresets.json`'s `environment` block or the shell):
   - `ANDROID_SDK_ROOT` — the Android SDK install root.
   - `ANDROID_NDK_ROOT` — the specific NDK version's directory under
     `$ANDROID_SDK_ROOT/ndk/<version>`.
   - `QT_ANDROID_ROOT_DIR` — the Android-specific Qt kit, e.g.
     `C:/Qt/6.9.2/android_arm64_v8a`.
   - `QT_ROOT_DIR` — a desktop Qt 6.9.2 kit (`mingw_64` or `msvc2022_64`;
     already set for the desktop presets). Needed as the **host** Qt for
     `moc`/`rcc`/`androiddeployqt`, which must run as native binaries even
     when the app itself is cross-compiled for arm64.
   - `JAVA_HOME` — the JDK 17 install from step 3.

## Configuring and building

Once the environment above is in place:

```powershell
cmake --preset android-arm64
cmake --build --preset android-arm64
```

This should (unvalidated — see the top of this file) produce a debug APK
under `build/android-arm64/android-build/build/outputs/apk/debug/`, signed
with Gradle's auto-generated debug keystore (fine for iteration, not for
distribution). `build/android-arm64-release` from the `android-arm64-release`
preset is the Release counterpart — release signing needs a real keystore
and is not set up yet (T45 still needs to document that procedure, the same
way `CONTRIBUTING.md`'s Flatpak section documents its own signing gotchas).

If `androiddeployqt` fails immediately, the most likely causes are: the
preset's environment variables above aren't actually set, `QT_HOST_PATH`
resolves to a Qt kit built with a different Qt version than
`QT_ANDROID_ROOT_DIR`, or the NDK version doesn't match what the installed
Qt Android kit expects (see the NDK row above).

## What's still open (T45 onward)

- Confirm the exact NDK patch version the installed Qt 6.9.2 Android kit
  actually expects, and correct the table above.
- Actually run the configure/build above once the toolchain exists, and fix
  whatever the (unvalidated) preset gets wrong.
- Release signing procedure (keystore generation, where it's stored, how
  CI would use it if this is ever automated).
- Install/launch smoke test on an emulator and a real arm64 device (T45's
  own acceptance criterion).
- T46-T52: runtime permissions, touch adaptation, background suspend/
  resume, and real-device TCP/BLE validation — all need an actual device
  and are unstarted.
