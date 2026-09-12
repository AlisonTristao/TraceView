# TraceView

Real-time telemetry dashboard for ESP32/ESP-NOW robots, built in C++ with Qt.

![TraceView screenshot](docs/images/example.png)

## What it does

TraceView connects to a robot (directly or through a dongle acting as a hub)
and turns its telemetry into a live dashboard:

- **Live charts, gauges and controls** for whatever topics/fields the device
  declares, laid out on a dashboard you build per project.
- **Multiple devices at once** — each with its own connection, its own
  dashboard tab, and its own serial terminal.
- **Hub support** — one cable to the dongle can carry several robots behind
  it; each still shows up as its own device with its own manifest, charts
  and terminal, and each recovers on its own if it reboots or drops out of
  range.
- **Firmware updates over Wi-Fi** from the OTA tab, with live reachability
  and per-device progress.
- **Log viewer** for a robot's `.blog` event log, and a multi-tab serial
  monitor for raw device output.
- **Settings** for render rate, terminal, connection and diagnostic
  preferences, plus themes and light/dark appearance.

## How it works

TraceView speaks [BTP](https://github.com/AlisonTristao/BTP) to whatever it
is connected to — a device over serial or USB HID. Each device advertises a
catalog of topics and fields (its manifest); TraceView subscribes to the
ones a dashboard widget needs and decodes the incoming frames into values
that widget understands.

The dashboard/UI layer never talks to BTP directly — it sits behind a
`Backend` interface (`lib/backend/backend.h`), with `BtpBackend` as the
concrete implementation. That keeps protocol/transport code isolated from
the UI, and is what lets a hub transparently multiply one cable into
several independent devices: each node behind it gets its own `Backend`
instance, sealed end-to-end, with the hub only relaying bytes it can't
read.

See [docs/DEVICES.md](docs/DEVICES.md) for the device/hub contract in
detail, [docs/PROTOCOL.md](docs/PROTOCOL.md) for the wire format, and
[docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for how this fits with the rest of
the Bally firmware.

## Download

Prebuilt Windows and Linux builds are published on the
[GitHub Releases page](https://github.com/AlisonTristao/TraceView/releases):

- **Windows** — an NSIS installer bundling the Qt/MinGW runtime; no separate
  Qt install needed.
- **Linux** — a `.tar.gz` bundling Qt and its own dependencies; requires the
  X11/Wayland/GL stack a Linux desktop already has.

TraceView checks for a newer release on startup (once a day at most) and
from **Settings ▸ Updates**; it only prompts, nothing installs without
clicking "Update Now".

Building from source instead? See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
