# TraceView Flatpak (x86_64)

Flatpak supplies Qt through the KDE runtime across Linux distributions. AppImage
remains the portable package; Windows and AppImage keep their existing updater.
This is a development/GitHub Releases manifest, not a published Flathub app.

Install `flatpak` and `flatpak-builder` using your distribution's package manager,
then run these commands from the repository root on Linux x86_64:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
flatpak-builder --user --install --force-clean --repo=flatpak-repo build-flatpak packaging/flatpak/io.github.alisontristao.TraceView.yml
flatpak run io.github.alisontristao.TraceView
flatpak info --show-permissions io.github.alisontristao.TraceView
```

The builder fetches pinned BTP, hidapi, and mbedTLS sources (including git
submodules) before CMake runs. CMake uses those local sources with FetchContent
fully disconnected. CI also separates download and sandboxed build phases.

To export a bundle, replace `X.Y.Z` with the release version:

```sh
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo flatpak-repo TraceView-X.Y.Z-linux-x64.flatpak io.github.alisontristao.TraceView stable
flatpak install --user ./TraceView-X.Y.Z-linux-x64.flatpak
dbus-run-session -- bash scripts/smoke_linux_flatpak.sh ./TraceView-X.Y.Z-linux-x64.flatpak
```

Standalone bundles require manually installing newer bundles. Automatic updates
with `flatpak update` require a publishing repository, such as Flathub. The app
never downloads or replaces itself with an AppImage in a Flatpak installation.

Network access supports OTA and mDNS. Device access supports serial ports and
hidraw, but host udev rules and serial group permissions still apply. There is
no blanket home/host filesystem permission; use the native file chooser portal.
Add `/run/udev:ro` only if hardware testing demonstrates enumeration needs it.

Before declaring Linux support validated, test Ubuntu, Fedora, Arch and Mint
on X11 and Wayland. CI currently checks headless startup on Ubuntu only:

- [ ] Window, icon, themes and translations
- [ ] Save/open `.tvproj`, read `.blog`, choose firmware `.bin` via portal
- [ ] Serial enumeration, ttyACM/ttyUSB connections and unplug/reconnect
- [ ] USB HID enumeration, hidraw access and hub/dongle communication
- [ ] OTA HTTP, mDNS and HTTPS/TLS
- [ ] Settings shows Flatpak-managed updates with no self-update action
- [ ] Existing AppImage and Windows packages still pass their smoke tests

For Flathub submission, replace the local `dir` source with an exact upstream
tag/commit, recheck KDE runtime support, update AppStream releases, run Flathub
linters, and justify device access. Publication and aarch64 support are separate
steps after functional validation.
