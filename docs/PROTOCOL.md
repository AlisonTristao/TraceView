# Serial protocol

TraceView's inbound wire format is the **Bally Telemetry Protocol (BTP) v1**,
whose canonical, cross-repo specification lives in
[`bally_protocol/docs`](../../bally_protocol/docs) (a sibling checkout —
see [`ECOSYSTEM.md`](ECOSYSTEM.md)):

- [`BTP_V1.md`](../../bally_protocol/docs/BTP_V1.md) — frame envelope, CRC,
  identity/sequence/timestamp rules, fragmentation.
- [`TELEMETRY.md`](../../bally_protocol/docs/TELEMETRY.md) — the `TELEMETRY`
  payload: `schema_version` + encoded body, schema/field model, `PACKED_LE`
  encoding, client field binding.
- [`STREAM_AND_REASSEMBLY.md`](../../bally_protocol/docs/STREAM_AND_REASSEMBLY.md)
  and [`TRANSPORT_SERIAL.md`](../../bally_protocol/docs/TRANSPORT_SERIAL.md) —
  COBS framing over the serial stream, the incremental decoder, and the
  console/protocoled-mode handshake.
- [`COMMANDS_AND_ACTIONS.md`](../../bally_protocol/docs/COMMANDS_AND_ACTIONS.md)
  — `COMMAND`/`CONTROL`/`TERMINAL` payloads, `HELLO` negotiation, manifest.

TraceView does not keep its own copy of that spec or a second codec — it
vendors `bally_protocol` as a CMake subdirectory and links `btp::codec` (see
the root `CMakeLists.txt`). This file only documents how TraceView's own
modules implement the client side of that contract; it is not itself a wire
format definition anymore (that role belonged to the pre-BTP `[<time>][<id>]
<payload>` line envelope this project used before topico 14 of the BTP
migration — see git history if you need that old grammar for archaeology).

## Client-side layering (topico 14)

```
SerialManager           -- raw QSerialPort bytes in/out, no framing knowledge
      |  dataReceived(QByteArray)
      v
BtpSession               -- lib/protocol/btpsession.h
                             incremental COBS decode (btp::SerialDecoder),
                             envelope/CRC validation, fragment reassembly
                             (btp::Reassembler); emits BtpFrame
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

`TelemetryCatalog` (lib/protocol/telemetrycatalog.h) is the source/topic/
schema registry -- independent of any widget type. It has no automatic
population yet: there is no `MANIFEST_REQUEST`/`MANIFEST_DATA` exchange on
the client side until topico 16 ("Manifesto e descoberta"), so
`registerBallySoftwareCatalog()` exists only as a convenience for tests and
tools that already know `bally_software`'s two documented schemas
(`protocol.test`, `robot.state` — see `TELEMETRY.md` section 9.4).

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

## Outbound: control commands

This part of the contract is **unchanged** by topico 14.
`PushButtonWidget`/`ToggleSwitchWidget`/`SliderWidget` still send their
configured literal command text straight through `SerialManager::
writeCommand()` as raw bytes, terminated by the global line-terminator
setting (Run ribbon tab):

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
- **Line terminator is a single global setting** (`LineTerminator`, now
  declared directly in `lib/core/serialmanager.h` since the old
  `serialprotocol.h` it used to live in was removed), tied to the port
  connection (Run ribbon tab), not per-widget. Options: None, `\n` (LF),
  `\r` (CR), `\r\n` (CRLF). Default: `\n`.
- This terminator setting only applies to control-widget-triggered
  commands. It does **not** apply to `SerialTerminalWidget`, which as of
  topico 19 no longer sends raw bytes at all (see "Terminal" below).

**Known gap, deliberately out of scope for topico 14/19:** this raw-text
path is not itself a BTP `COMMAND` frame. On real hardware running a dongle
in protocoled mode (topico 13's `SerialMux`, which owns the port exclusively
once negotiated), writing arbitrary raw bytes alongside `BtpSession`'s COBS
frames would corrupt the stream. Migrating control-widget output onto BTP's
`COMMAND`/`COMMAND_REQUEST` → `COMMAND_RESULT` exchange (see
`COMMANDS_AND_ACTIONS.md` section 4) is tracked as its own future topico
("Acoes persistidas e comandos virtuais"), not folded into this one.

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
`bally_protocol/topicos/19_terminal_protocolado.txt` RESULTADO for the
PASSO 1/2 design decision and why). `SerialTerminalWidget` only forwards
keystrokes and renders whatever `TERMINAL_OUT` sends back, using a minimal
line model equivalent to a VT100 subset (`\r` returns to column 0 without
erasing, `\b` moves the cursor left one column, `\n` commits the line,
anything else overwrites/extends at the current column) -- no ANSI CSI
sequences are needed because `ShellSerial` never emits any.

Each `SerialWidgetBridge` instance owns a private, random, non-zero
`source_id`/`boot_id` pair for `TERMINAL_IN` frames only -- there is still
no `HELLO` negotiation on the TraceView side (topicos 15-17), so nothing
here assumes or is assigned a real client identity yet.
