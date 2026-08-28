# Serial protocol

TraceView's inbound wire format is the **Binary Telemetry Protocol (BTP) v1**,
whose canonical, cross-repo specification lives in
[`BTP/docs`](../../BTP/docs) (see [`ECOSYSTEM.md`](ECOSYSTEM.md)):

- [`frame.md`](../../BTP/docs/frame.md) and
  [`model.md`](../../BTP/docs/model.md) — frame envelope, CRC,
  identity/sequence/timestamp rules.
- [`telemetry.md`](../../BTP/docs/telemetry.md) — the `TELEMETRY`
  payload: `schema_version` + encoded body, schema/field model, `PACKED_LE`
  encoding, client field binding.
- [`fragmentation-and-transports.md`](../../BTP/docs/fragmentation-and-transports.md)
  — fragmentation and reassembly, COBS framing over the serial stream, the
  incremental decoder, and the three transport profiles.
- [`commands.md`](../../BTP/docs/commands.md)
  — `COMMAND`/`CONTROL` payloads and the manifest.
- [`session-and-terminal.md`](../../BTP/docs/session-and-terminal.md) —
  `HELLO` negotiation, the console/protocoled-mode handshake, and `TERMINAL`.

TraceView does not keep its own copy of that spec or a second codec — it
fetches `BTP` via CMake `FetchContent`, pinned to a released tag, and links
`btp::codec` (see the root `CMakeLists.txt`). This file only documents how TraceView's own
modules implement the client side of that contract; it is not itself a wire
format definition anymore (that role belonged to the pre-BTP `[<time>][<id>]
<payload>` line envelope this project used before topico 14 of the BTP
migration — see git history if you need that old grammar for archaeology).

## Client-side layering (topico 14)

```
SerialManager/UsbHidManager -- raw bytes in/out, no framing knowledge
                             (Transport, lib/core/transport.h -- either
                             concrete class, chosen once per device by
                             Device::transportType)
      |  dataReceived(QByteArray)
      v
BtpSession               -- lib/protocol/btpsession.h
                             built with the matching btp::TransportProfile:
                             Serial mode does incremental COBS decode
                             (btp::SerialDecoder); UsbHid mode decodes each
                             already-bounded HID report directly
                             (btp::decode(), no COBS --
                             fragmentation-and-transports.md 3.3).
                             Both do envelope/CRC validation and fragment
                             reassembly (btp::Reassembler); emits BtpFrame
      |  frameReceived(BtpFrame)
      v
ProtocolRouter            -- lib/protocol/protocolrouter.h
                             dispatches by MessageType; for TELEMETRY, splits
                             the 2-byte schema_version prefix out into a
                             TelemetrySample (payload stays opaque bytes,
                             never converted to UTF-8)
      |  telemetrySampleReceived(TelemetrySample)
      v
TelemetryFieldRouter       -- lib/protocol/telemetryfieldrouter.h
                             resolves (source_id, topic_id, schema_version)
                             against a TelemetryCatalog, decodes PACKED_LE
                             (lib/protocol/packedledecoder.h), and broadcasts
                             one fieldSample(binding, timestampUs, value) per
                             decoded field/element -- a plain Qt signal, so
                             any number of subscribers (chart/gauge widgets)
                             can connect to the same field independently
```

As of the multi-device connection refactor, this whole stack (`SerialManager`
through `BtpBackend`'s internal `TelemetryFieldRouter`) is instantiated once
**per device** by `DeviceConnection` (`lib/core/deviceconnection.h`), not
once for the whole app -- see [docs/DEVICES.md](DEVICES.md#connections). A
dashboard widget's own `deviceId` config picks which device's pipeline (and
therefore which `TelemetryCatalog`/`fieldSample` stream) it subscribes to;
`source_id`/`topic_id` below are still only unique *within* one device's
session.

`TelemetryCatalog` (lib/protocol/telemetrycatalog.h) is the source/topic/
schema registry -- independent of any widget type. As of topico 16
("Manifesto e descoberta") it is populated dynamically: `ManifestClient`
(lib/protocol/manifestclient.h) requests the whole catalog
(`MANIFEST_REQUEST` with `target_source_id=0`) once per session -- skipped on
a reconnect if the dongle's own `HELLO_RESULT.config_revision` hasn't changed
since the last one this process saw -- parses each `MANIFEST_DATA` response
(one message per source) and registers a `TelemetryTopicSchema` per topic
record, reusing the wire's own types/units/scale/offset rather than any
hardcoded table. `TelemetryFieldRouter::unknownSchema` triggers a targeted,
rate-limited re-request when a sample's `schema_version` isn't in the
catalog. `registerBallySoftwareCatalog()` still exists, but only as a
convenience for tests and tools that want Bally_OS's two static schemas
(`protocol.test`, `robot.state`) without a live dongle connection;
`MainWindow` no longer calls it. Those two topic names are this product's
own convention, defined by bally_OS's `TelemetryPublisher` — they are not
part of the BTP specification, whose `telemetry.md` uses an unrelated worked
example; BTP only keeps a canonical `protocol.test` frame under
`test-vectors/v1/valid/`.

Wiring a chart/gauge widget's own `sourceId`/`topicId`/`fieldId` config
(`ChartConfigEditor`/`GaugeConfigEditor`, `lib/dashboard/widgets/
chartdata.h`) onto a live `TelemetryFieldRouter::fieldSample` subscription is
**topico 15**'s job ("fatia vertical de telemetria binaria"), not this one —
topico 14 only defines the protocol/data-model layer and leaves the widgets'
`appendFieldSample()` entry points ready for it.

Diagnostics: `BtpSession::diagnostics()` (CRC/frame/COBS/overflow/reassembly
counters), `ProtocolRouter::diagnostics()` (routed/dropped per channel), and
`TelemetryFieldRouter::diagnostics()` (schema-unknown/decode-error counters)
are all available for a future diagnostics panel; none is wired into the UI
yet (no concrete need for one until topico 15/16 land).

## Serial line state: DTR (topico 35 F1)

The dongle is native USB-CDC (`ARDUINO_USB_MODE=0`). On that stack
`tud_cdc_n_connected()` tests the **DTR bit alone**, and `USBCDC::write()`
returns 0 — silently dropping every byte, `BTP/1 READY` included — until the
host asserts DTR. RX is unaffected. `QSerialPort`'s default DTR/RTS state
after `open()` varies by platform and Qt version, so `SerialManager::open()`
(`lib/core/serialmanager.cpp`) asserts `setDataTerminalReady(true)` +
`setRequestToSend(true)` explicitly, once, right after the port opens (and
`setRequestToSend(false)` then `setDataTerminalReady(false)` on `close()`, in
that order, so the CDC line-state machine never steps toward its reset
sequence). `open()` also folds a configured 1200 baud up to 115200: 1200 is
the CDC's bootloader-touch, never a working data rate here.

## Handshake and version negotiation (topico 15)

`BtpHandshake` (`lib/protocol/btphandshake.h`) drives the plain-text
`BTP/1 ENTER`/`BTP/1 READY` exchange (`session-and-terminal.md` section 3)
followed by `HELLO`/`HELLO_RESULT` (`session-and-terminal.md` sections 1-2) on
top of an already-open serial connection, before anything else (manifest,
subscriptions, terminal) is allowed on the wire.

### Keeping the session alive (topico 35 B)

The dongle drops a serial session back to console after `session_timeout_ms`
(negotiated `min` of both ends, 30000 today) with no valid BTP frame received
— and it has no keepalive of its own. `BtpBackend` therefore runs a 5s
`QTimer`, restarted by every outbound frame (a `BtpSession::bytesToWrite`
hook) so an active session with subscription renewals / commands / terminal
traffic never pays for it; only a genuinely idle link fires
`sendSessionKeepalive()`, which sends a `MANIFEST_REQUEST` for a sentinel
`source_id` (`0xFFFFFFFF`) — the dongle answers a small `NOT_FOUND` that
`ManifestClient` discards, so the exchange renews the watchdog and nothing
else. On the other side, `BtpHandshake::feedRawBytes()` scans the raw byte
stream for `BTP/1 CONSOLE\r\n` while `Established` (the dongle prints exactly
that on any drop — watchdog, `SESSION_CLOSE`, a bench human) and folds it into
the same `sessionFailed` → recovery path an exhausted handshake uses.

`HELLO`'s `versions` field is **not** hardcoded to `[1]`: it lists every
value from `btp::kMinimumProtocolVersion` to `btp::kMaximumProtocolVersion`
(`build/_deps/btp-src/include/btp/codec.hpp`) ascending, i.e. the *entire*
range of BTP versions this build's linked codec can speak. The dongle is the
one that picks — `HELLO_RESULT.selected_version` is "a maior versao comum"
between what we advertised and what it supports — so TraceView's job is only
to make its whole compatibility range visible, never to pre-guess which
version will be chosen. `BtpHandshake` in turn rejects a `HELLO_RESULT`
whose `selected_version` falls outside the range it just advertised
(`sessionFailed`, not a silent accept) — a peer answering with a version we
never offered isn't a compatible peer. Both bounds equal `1` today, so in
practice this is currently a one-entry list; it widens on its own the day
`btp::codec` gains a newer version, with no change needed here.

## Subscriptions and rate control (topico 17)

Nothing above puts a topic *on* the wire; that is `SubscriptionManager`
(`lib/protocol/subscriptionmanager.h`). Its model is a reference count per
`(source_id, topic_id)`, not one subscription per widget — several charts and
gauges routinely plot different fields of the same topic, and the wire only
ever carries whole topics:

- the first consumer of a topic triggers one `SUBSCRIBE` carrying the highest
  rate any live consumer asked for (a chart derives that from its configured
  sample time `Ts`; a gauge asks for a fixed 5 Hz);
- another consumer of the same topic adds no traffic unless it wants a
  *higher* rate, in which case one new `SUBSCRIBE` (new `sequence`) replaces
  the previous one atomically;
- closing one of several consumers sends nothing, except a rate-*lowering*
  `SUBSCRIBE` when the one that left was the one asking for the top rate;
- only the last consumer leaving a topic sends `UNSUBSCRIBE`.

`MainWindow` is the only place that knows about widgets: `widgetCreated` and
each widget's own `destroyed()` add/remove one opaque consumer handle, and the
undo stack's `indexChanged` re-derives every handle after a config edit (or
its undo/redo) repoints a widget at another source/topic or changes `Ts`.

`SUBSCRIBE` needs a non-zero `target_boot_id`, which only `MANIFEST_DATA`
supplies, so `TelemetryCatalog` now also records a `sourceBootId()` per source
(written by `ManifestClient`); a subscription requested before that manifest
arrived is held back and released on `ManifestClient::catalogUpdated`.
Subscriptions are scoped to the BTP session that granted them: a disconnect
forgets the grants but not the consumers, and a new `sessionEstablished()`
re-subscribes everything still open. Leases are renewed at half the granted
lease while a consumer exists.

The granted rate is never assumed equal to the requested one: the
`effective_rate_millihz` of `SUBSCRIBE_RESULT` is what the status bar shows
(flagged as "limited" with the rate that was asked for when it came back
lower), and a rejection surfaces its status/error code. `CONTROL/STATUS` with
`status_version=2` (`commands.md` section 5.1) adds per-topic
subscriber count, effective rate, bytes and drops as measured *at the source*,
shown in the status bar's tooltip; a `status_version=1` emitter still parses
correctly — the reader stops at 92 octets and simply has no per-topic data.
`lib/protocol/statusreport.h` is the standalone parser for both versions.

## Outbound: control commands

This part of the contract is **unchanged** by topico 14.
`PushButtonWidget`/`ToggleSwitchWidget`/`SliderWidget` still send their
configured literal command text straight through `SerialManager::
writeCommand()` as raw bytes, terminated by the target device's own
line-terminator setting -- `SerialWidgetBridge` (`lib/core/serialwidgetbridge.h`)
resolves which device's `SerialManager` that is from the widget's own config
(`deviceId`), see [docs/DEVICES.md](DEVICES.md):

```
<literal command text><line terminator>
```

- The command body is sent exactly as configured by the user — no envelope,
  no id, no brackets. It is not addressed to a widget on the device side;
  the app has no way (and no need) to know how the device's firmware
  demuxes it.
- **One placeholder is defined**: `{value}` — recognized only in
  `SliderConfigEditor::commandTemplate`, replaced with the slider's current
  value converted via `QString::number` at send time. No other config field
  (`onPress`/`onRelease`/`onCommand`/`offCommand`/long-press command)
  supports a placeholder — they are opaque literal strings, sent as-is.
- **Line terminator is a per-device setting** (`LineTerminator`, declared in
  `lib/core/serialmanager.h`), tied to that device's connection
  (`DeviceConfigDialog`, see [docs/DEVICES.md](DEVICES.md)) rather than a
  single global setting or a per-widget one -- every control-widget command
  sent to a given device uses that device's configured terminator,
  regardless of which widget sent it. Options: None, `\n` (LF), `\r` (CR),
  `\r\n` (CRLF). Default: `\n`.
- This terminator setting only applies to control-widget-triggered
  commands. It does **not** apply to `SerialTerminalWidget`, which as of
  topico 19 no longer sends raw bytes at all (see "Terminal" below).

**Known gap, deliberately out of scope for topico 14/19:** this raw-text
path is not itself a BTP `COMMAND` frame. On real hardware running a dongle
in protocoled mode (topico 13's `SerialMux`, which owns the port exclusively
once negotiated), writing arbitrary raw bytes alongside `BtpSession`'s COBS
frames would corrupt the stream. Migrating control-widget output onto BTP's
`COMMAND`/`COMMAND_REQUEST` → `COMMAND_RESULT` exchange (see
`commands.md` section 2) is tracked as its own future topico
("Acoes persistidas e comandos virtuais"), not folded into this one.

**USB HID devices have no equivalent path at all**, not even the corruption
risk above: `fragmentation-and-transports.md` section 3.3 defines the
`usb_hid` profile
as always BTP-protocolled, with no console/raw-byte mode to send arbitrary
text into in the first place. A `Device` with `transportType ==
TransportType::UsbHid` has no `SerialManager` (`DeviceConnection::
serialManager()` returns `nullptr`), so `SerialWidgetBridge` just doesn't
send anything for that device's control widgets -- same "went nowhere, not
an error" contract a closed port already had. `SerialMonitorWidget` is
unaffected either way: its inbound/outbound path already goes through
`Backend::sendTerminalIn()`/`terminalDataReceived` (a real BTP `TERMINAL`
frame), which both transports support identically.

## Terminal (topico 19)

`SerialTerminalWidget` (`lib/dashboard/widgets/serialterminalwidget.h`) no
longer talks to `SerialManager` directly. `SerialWidgetBridge`
(`lib/core/serialwidgetbridge.h`) wires it onto the BTP `TERMINAL_IN`/
`TERMINAL_OUT` channel instead:

```
SerialTerminalWidget::sendRequested(QByteArray)   -- raw keystroke(s)/escape bytes
      v
SerialWidgetBridge::sendTerminalIn()               -- wraps as a MessageType::Terminal
                                                       frame, object_id TERMINAL_IN
                                                       (0x0001), and calls
      v
BtpSession::sendFrame()

ProtocolRouter::terminalFrameReceived(BtpFrame)    -- object_id TERMINAL_OUT (0x0002) only
      v
SerialTerminalWidget::appendData(QByteArray)
```

Line editing (echo, backspace, arrow-key history/cursor movement, Tab
completion, Ctrl+R reverse search) is **not** implemented in TraceView --
it stays entirely on the dongle's `ShellSerial` (see
`BTP/topicos/19_terminal_protocolado.txt` RESULTADO for the
PASSO 1/2 design decision and why). `SerialTerminalWidget` only forwards
keystrokes and renders whatever `TERMINAL_OUT` sends back, using a minimal
line model equivalent to a VT100 subset (`\r` returns to column 0 without
erasing, `\b` moves the cursor left one column, `\n` commits the line,
anything else overwrites/extends at the current column) -- no ANSI CSI
sequences are needed because `ShellSerial` never emits any.

Each `SerialWidgetBridge` instance owns a private, random, non-zero
`source_id`/`boot_id` pair for `TERMINAL_IN` frames only, independent of
`BtpHandshake`'s own negotiated identity (topico 15, `lib/protocol/
btphandshake.h`) -- `BtpHandshake` does not expose the peer/session identity
it negotiated for reuse elsewhere, so nothing here is assigned that real,
negotiated client identity yet.
