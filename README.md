# TraceView

Real-time telemetry dashboard for ESP32/ESP-NOW robots, built in C++ with Qt.

> **Status:** v2.4.0 — a new Settings tab centralizes rendering, terminal,
> connection and diagnostic preferences alongside the existing appearance
> controls. The dongle can also act as a hub: one cable carries several
> robots, each with its own device, charts, terminal and end-to-end seal.
> Firmware uploads over Wi-Fi from the OTA tab. See
> [CONTRIBUTING.md](CONTRIBUTING.md) for the branching/feature workflow and
> [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for how this project fits with the
> rest of the Bally ecosystem.

## What's new (unreleased)

- **Settings center** — the new **Settings** ribbon tab groups General,
  Appearance, Dashboard, Terminal, Connections and Diagnostics preferences.
  Pick Low (15 FPS), Medium (30 FPS), High (60 FPS), or a custom redraw cap;
  tune terminal scrollback and reconnect behavior without hunting through
  code. Theme/font changes apply immediately, while the page offers a restart
  when language or diagnostic history changes need a fresh session.
- **Hub children recover on their own** — a robot behind a hub that reboots
  or drops out of range no longer needs a manual reconnect. Its card goes
  amber while it is quiet and back to green on its own, re-fetching the
  catalog and re-subscribing every chart against the new boot. See
  [Talking to TraceView: the device contract](#talking-to-traceview-the-device-contract)
  for the scheme.

## What's new in 2.3.0

- **Hub channels** — a device can connect *through* another device instead
  of over a wire of its own. The desktop opens one connection to the dongle
  and talks to it as an ordinary BTP device; every robot behind its radio
  becomes its own device, with its own manifest, charts and terminal,
  riding that same single cable. Nothing re-encodes or re-fragments in
  either direction, so an end-to-end seal still verifies at the far end —
  the dongle holds no key for the traffic it carries. See
  [docs/DEVICES.md](docs/DEVICES.md).
- **Live robot picker** — pick a robot behind the hub from a list of the
  ones it has actually heard (channel, address, online/offline with an age,
  MAC on hover), decoded from the dongle's own `hub.peers` telemetry, and
  refreshed while the dialog is open. Typing an address by hand still works
  for a robot the hub hasn't heard yet.
- **OTA Update tab** — push a firmware `.bin` to a robot over Wi-Fi from
  File → Upload Firmware (OTA)…, with live reachability, the version each
  robot reports it is running, and a progress bar per upload. Resolves
  `*.local` addresses with its own mDNS query, so it works on Windows
  without Bonjour installed. See [docs/OTA.md](docs/OTA.md).
- **Sealed channels** — per-channel key derivation shared byte-for-byte
  with bally_OS and the dongle, so which key opens which message is one
  table agreed by all three (`include/bally_channels.h`, hash-checked in
  each repository).

## What's new in 2.2.0

- **USB HID transport** — connect to a device over USB HID instead of a
  serial port, picked from a new Transport combo when adding/editing it.
  Speaks BTP v1.1.0's `usb_hid` profile to a dongle exposing a composite
  CDC+HID USB device. USB HID devices have no raw-text console channel, so
  control-widget text commands go nowhere for them — everything else
  (telemetry, the serial monitor) works the same as over serial. See
  [docs/DEVICES.md](docs/DEVICES.md).
- **Logs tab** — open a bally_OS `.blog` file (the robot's SD-card event
  log) and read every entry in a table: timestamp, severity, source/boot
  id, sequence and message text. Multi-fragment messages are reassembled
  automatically; a corrupted or truncated frame is skipped instead of
  failing the whole file.

## What's new in 2.1.0

- **Devices tab** — devices are their own managed list now (name, connection
  config, live status dot), instead of one port/baud pair for the whole
  project. See [docs/DEVICES.md](docs/DEVICES.md).
- **Multiple simultaneous connections** — each device opens its own
  independent BTP session and reconnects on its own in the background, so
  several robots/boards can be live at once.
- **Per-widget device targeting** — every chart, gauge, control, and the
  serial monitor picks which device it listens to; there's no single
  "active device" for a project anymore.
- Devices tab undo/redo, and **Ctrl+Tab**/**Ctrl+Shift+Tab** to cycle
  workspaces from anywhere.

See [CHANGELOG.md](CHANGELOG.md) for the full list, including every prior release.

## Architecture: the UI doesn't know about BTP

TraceView ships wired to [BTP](https://github.com/AlisonTristao/BTP) by
default, but the dashboard/UI layer (`traceview_dashboard`, `traceview_ui`)
never talks to BTP directly. Everything that gives meaning to the bytes
coming off the wire — decoding telemetry samples, tracking topic
subscriptions, framing terminal input/output — sits behind one abstract
interface:

```
lib/backend/backend.h   -> class Backend (the interface)
lib/protocol/btpbackend.h/.cpp -> class BtpBackend : public Backend (the BTP implementation)
lib/telemetry/          -> generic value types Backend's API is expressed in
                            (TelemetryFieldBinding, TopicSubscriptionState,
                            StatusTopicRecord, TelemetrySeriesBuffer) — no
                            BTP dependency
```

`core/serialmanager.h` (`SerialManager`) stays a concrete piece with a
narrow job: it only moves raw bytes in and out of one open serial port,
nothing more. `Backend` is the layer above it, fed raw bytes (`feedBytes()`)
and connection state (`onTransportConnectionChanged()`), driving the
dashboard through its signals (`fieldSample`, `subscriptionsChanged`,
`terminalDataReceived`, ...) — see `lib/backend/backend.h` for the full
contract. `core/deviceconnection.h` (`DeviceConnection`) pairs one
`SerialManager` with one `Backend` per configured device
(see [docs/DEVICES.md](docs/DEVICES.md)), so `MainWindow` can hold several
devices open and decoding independently at once instead of one shared
connection for the whole project.

This means a different protocol can be plugged in by implementing `Backend`
and constructing it in place of `BtpBackend` in `MainWindow`'s constructor —
nothing else in the UI/dashboard layer needs to change. A from-scratch
`Backend` implementation depends only on `traceview_backend` and
`traceview_telemetry` (both plain `Qt6::Core`, no BTP dependency), never on
`traceview_protocol`.

This boundary is still young — the interface will keep moving as more of
the protocol gets built out, and only BTP has a real implementation behind
it today.

## Talking to TraceView: the device contract

TraceView drives any device that speaks [BTP](https://github.com/AlisonTristao/BTP)
v1 and follows the conventions below. Two of those conventions ship as
headers every end compiles against so there is nothing to negotiate:
`include/bally_channels.h` (which key opens which frame) and the **names**
a hub gives its `hub.peers` topic and fields. Nothing here is specific to
any one firmware — a different board, radio or bus can implement the same
contract and TraceView will drive it unchanged.

### Topology

```
TraceView ──(cable: serial / USB)── direct device
TraceView ──(cable)── hub ──(radio / bus)── node
                          ──(radio / bus)── node
                          ── …
```

- **Direct device** — one cable, one BTP session, a handshake.
- **Hub** — a direct device that *also* relays for **nodes** it reaches
  over a secondary link. TraceView opens one cable to it and treats it both
  as a device of its own and as a multiplexer: each node behind it is a
  separate `Device` (`TransportType::HubChannel`), its frames riding that
  same cable, demuxed by address.
- **Node** — reachable only through a hub; never has its own cable to
  TraceView.

### Identity

Every device has a permanent **`source_id`** (non-zero) and a **`boot_id`**
that changes on every restart. Both sit in the clear at a fixed header
offset in *every* frame, sealed payload or not. All routing — the hub's
demux, TraceView's per-node fan-out, subscription addressing — keys on
`source_id`; `boot_id` tells one run of a device from the next.

### The three channels

A channel is defined by **which two ends talk**, never by message type
(`include/bally_channels.h`):

| Channel | Ends | Sealing |
|---|---|---|
| **Console** | TraceView ↔ hub (or ↔ direct device) | in the clear — it is a physical cable |
| **Endpoint** | TraceView ↔ node | per-node key, **end to end** — the hub cannot read it |
| **Link** | hub ↔ node | per-fleet key — relay administration only (heartbeat, catalog priming) |

The hub holds the Link key and no Endpoint key: it administers the relay
and reads nothing it carries end to end.

### Discovery

**Direct device** — BTP handshake (`ENTER` / `HELLO` / `HELLO_RESULT`).
`HELLO_RESULT` reports the negotiated envelope version and the device's
`source_id`. TraceView then enumerates the catalog (`MANIFEST_REQUEST` with
`target_source_id = 0`) and subscribes per widget.

**Node behind a hub** — *no handshake*; a node offers no console session.
When the child transport comes up TraceView sends a **targeted**
`MANIFEST_REQUEST` (`target_source_id` = the node, never `0` — only a hub
can answer an enumeration) and re-asks on a timer until `MANIFEST_DATA`
arrives. With no `HELLO_RESULT`, the card's identity comes from that
manifest's `config_revision` and `source_id`.

### What a hub must do

1. **Relay by default.** Every frame, either direction, is forwarded
   verbatim, demuxed only by header `source_id` — no routing table, no
   per-type case. **Nothing is re-encoded, re-fragmented, or has its CRC
   recomputed**, so an end-to-end seal still verifies at the far end.
2. **Consume only a short, explicit list** addressed to itself: a node's
   heartbeat (`STATUS`), a `MANIFEST_DATA` answering a request the hub
   itself issued, and `COMMAND`s naming the hub. Everything else relays.
   The list and the `reference_source_id == self` check live in
   `include/bally_channels.h`.
3. **Cache node manifests.** On first hearing a node — or after its
   `boot_id` changes — send it a `MANIFEST_REQUEST` over the Link channel
   and cache the reply. Retry fast at first, then steadily, and **never
   give up** while the node's `STATUS` keeps arriving.
4. **Serve targeted requests from that cache**, stamping the response
   header with **the node's `source_id`, not the hub's** — otherwise the
   demux hands it to the wrong child and the node's catalog never appears.
5. **Publish a `hub.peers` telemetry topic**: one row per node it has
   heard, fields named `channel`, `source_id`, `boot_id`, `mac`,
   `last_seen_ms` (an age, not a timestamp) and `online` (age below a few
   seconds). TraceView resolves the topic and every field **by name**, so
   the numeric ids are the hub's to choose and renumber.

### What a node must do

1. Emit `STATUS` on the Link channel about once a second — a sealed
   liveness heartbeat. TraceView never sees it directly; it reads the
   `online`/`last_seen_ms` the hub derives from it in `hub.peers`.
2. Answer `MANIFEST_REQUEST` with a self-contained `MANIFEST_DATA`:
   `source_id`, `boot_id`, `config_revision` (monotonic, starts at 1,
   bumps only when the topic/field catalog changes), then the topic and
   field records.
3. Seal telemetry and logs with the Endpoint key — end to end, no
   cleartext fallback.
4. Arbitrate `SUBSCRIBE` / `UNSUBSCRIBE` per session, each addressed to a
   `(source_id, boot_id)` pair, and stream the granted topics at the
   granted rate.
5. **Drop all subscription state on restart** (new `boot_id`). A
   `SUBSCRIBE` carrying a stale `boot_id` is rejected.

### Automatic recovery

A node behind a hub never handshakes, so a reboot or a drop out of range is
invisible to its child unless TraceView watches for it — which it does:

- While at least one hub child is connected, TraceView subscribes to that
  hub's `hub.peers` and reconciles it against every child once a second.
- **Node silent** (`online = false`) for ~8 s, debounced so a busy loop or
  one missed heartbeat doesn't flap → the child's card goes amber, "robot
  not responding"; the cable to the hub is untouched.
- **Node rebooted** (`boot_id` changed) → TraceView re-requests that node's
  catalog; when the fresh `MANIFEST_DATA` lands with the new `boot_id`,
  every subscription for that node is re-sent against it. The operator
  reconnects nothing. A node that merely dropped out of range and returned
  on the *same* boot needs nothing — its catalog is still valid and its
  subscriptions self-heal on the next lease renewal.
- A slow catalog re-poll (~20 s) is kept as a backstop for the case where
  the `hub.peers` subscription itself never resolves.

### Invariants a replicating device must not break

1. A hub stamps a cache-served `MANIFEST_DATA` with the **node's**
   `source_id`.
2. No frame is re-encoded anywhere on the path — seals and size ceilings
   are computed once, by the originator.
3. A client decoding hub-relayed frames measures "too large" against the
   **cable's** ceiling, not the secondary link's: a multi-topic
   `MANIFEST_DATA` is larger than one radio datagram.

See [docs/DEVICES.md](docs/DEVICES.md) for how this maps onto TraceView's
own `Device` / `DeviceConnection` / `BtpBackend` classes, and
[docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for the wider pipeline.

## Build

Requirements: CMake >= 3.21, Qt6 (Widgets, SerialPort), a C++17 compiler.

```sh
cmake -B build -S .
cmake --build build
```

If Qt6 isn't auto-discovered (e.g. a Qt Online Installer setup on Windows),
point CMake at the kit explicitly:

```sh
cmake -B build -S . -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/mingw_64"
cmake --build build
```

## License

[MIT](LICENSE)
