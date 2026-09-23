# macOS and iOS builds

TraceView builds for macOS and iOS from the same sources as the other
platforms. Both need a Mac with Xcode. CI builds both on `macos-14` runners
(`.github/workflows/build.yml`) and releases only macOS
(`.github/workflows/release.yml`).

## iOS: no serial, no USB HID

**The iOS build has no serial and no USB HID transport. This is permanent,
not a missing feature.** iOS does not let apps talk to USB-serial adapters
(CP210x, CH340, the ESP32-S3's own USB-CDC) or to raw USB HID devices. The
only route Apple allows is the MFi ExternalAccessory program, which requires
an Apple authentication chip inside the peripheral.

On iOS, TraceView reaches devices over:

- **BLE**: the BTP GATT service, directly to an ESP32-S3.
- **TCP**: a robot on the same Wi-Fi network.
- **Hub channel**: a child device behind one of the connections above.

The build enforces this. The `ios` preset sets `TRACEVIEW_ENABLE_SERIAL` and
`TRACEVIEW_ENABLE_USB_HID` to `OFF`, and on iOS they default to `OFF`
anyway. Configuring iOS with either one `ON` stops CMake with a
`FATAL_ERROR`. With both off, the device dialog does not offer Serial or USB
HID at all.

## Qt kits

Both platforms use Qt 6.9.2, the same version the Android build pins. Install
it with the Qt online installer or [aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
pip install aqtinstall==3.3.0
# macOS (also the host kit for iOS)
aqt install-qt mac desktop 6.9.2 clang_64 --modules qtserialport qtconnectivity --outputdir ~/Qt
# iOS
aqt install-qt mac ios 6.9.2 ios --modules qtconnectivity --outputdir ~/Qt
```

`qtconnectivity` provides Qt Bluetooth (BLE), which is on by default since
4.3.0.

## macOS

```sh
export QT_ROOT_DIR=~/Qt/6.9.2/macos
brew install ninja
cmake --preset macos
cmake --build --preset macos
ctest --preset macos
open build/macos/TraceView.app
```

- **Minimum:** macOS 12 (Qt 6.9's own floor). Release packages are Apple
  Silicon (`arm64`) only. An Intel build works from the `macos` preset on an
  Intel Mac.
- **Serial ports:** macOS exposes each device twice, as `/dev/cu.*` and
  `/dev/tty.*`. TraceView lists only `cu.*`, because opening `tty.*` waits
  for carrier detect.
- **Bluetooth permission:** the bundle's `Info.plist`
  (`resources/macos/Info.plist.in`) carries `NSBluetoothAlwaysUsageDescription`.
  macOS asks the first time you scan or connect over BLE. Without that key,
  macOS denies Bluetooth outright. This is also why CI skips
  `test_bletransport` on macOS: a bare test executable has no `Info.plist`.
- **Icon:** `resources/icons/app.icns` is generated from
  `resources/icons/app.svg`. Regenerate it when the SVG changes.

### Packaging

```sh
cmake --preset macos-release
cmake --build --preset macos-release
scripts/build_macos_dmg.sh build/macos-release 4.3.0
```

The script runs `macdeployqt`, **ad-hoc** signs the bundle and writes
`TraceView-<version>-macos-arm64.dmg`, which contains the app and an
Applications shortcut. `scripts/smoke_macos_dmg.sh` mounts it, installs it
and checks that the app stays running.

Ad-hoc signing is not Developer ID signing, so Gatekeeper blocks the first
launch of a downloaded copy. Right-click the app and choose **Open**, or run
`xattr -dr com.apple.quarantine /Applications/TraceView.app`. Removing that
step requires an Apple Developer account (US$ 99/year), a Developer ID
Application certificate, and a `codesign` + `xcrun notarytool` step in
`release.yml`.

**Updates:** the in-app updater downloads the release `.dmg`, verifies it
against `SHA256SUMS.txt`, opens it in Finder and quits TraceView. You then
drag the new copy over the old one in Applications.

## iOS

```sh
export QT_ROOT_DIR=~/Qt/6.9.2/macos
export QT_IOS_ROOT_DIR=~/Qt/6.9.2/ios
cmake --preset ios
open build/ios/TraceView.xcodeproj
```

In Xcode, choose your team under **Signing & Capabilities**, connect the
device and click **Run**. A free Apple account works, but the app expires
after 7 days. A paid account allows 1-year installs, TestFlight and Ad Hoc
distribution.

To build without signing (what CI does):

```sh
cmake --build --preset ios -- CODE_SIGNING_ALLOWED=NO
```

- **Permissions:** `resources/ios/Info.plist.in` declares
  `NSBluetoothAlwaysUsageDescription` (BLE) and
  `NSLocalNetworkUsageDescription` (TCP to a robot on the LAN).
- **Icon:** `resources/ios/Assets.xcassets`, a single opaque 1024 px image.
- **No self-update:** iOS apps may not install updates themselves. The
  updater finds no iOS asset and offers the release page instead.
- **Mobile UI:** iOS uses the same compact chrome as Android, and the same
  "close and reopen" flow where a desktop build restarts itself.
- **Licensing:** Qt for iOS links statically. Under the LGPL, anyone
  distributing the app must let users relink it against a modified Qt. For
  example, publish the object files, or distribute the source, which this
  MIT-licensed project already does.

## Status

| | macOS | iOS |
|---|---|---|
| Compiles in CI | yes | yes (unsigned) |
| Unit tests in CI | yes (except `test_bletransport`) | no (no simulator run) |
| Release package | `.dmg`, ad-hoc signed | none |
| Tested on real hardware | **not yet** | **not yet** |
| BLE tested against the ESP32-S3 | **not yet** (T37) | **not yet** (T37) |
