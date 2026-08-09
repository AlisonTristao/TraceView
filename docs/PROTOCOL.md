# Serial protocol

This document closes the wire format for the single global serial connection
(see [BACKEND_TODO.txt](../BACKEND_TODO.txt) for the full backend plan — this
is Task 1, spec only, no production code). It is the contract that
`SerialProtocol::decodeFrame` (Task 4) and the per-widget payload consumers
(Tasks 7/8) implement against, and that `SerialManager::write` callers
(Task 9) produce.

There are two independent directions, covered separately below: **inbound**
frames (device → app, routed to widgets by key) and **outbound** commands
(app → device, triggered by control widgets).

## Inbound: frame envelope

```
[<time>][<id>] <payload><EOL>
```

- `<time>` — non-negative decimal integer, milliseconds, device-generated
  free-running clock (e.g. `millis()`). Purely informational: it is shown in
  the terminal for debugging and passed through `decodeFrame`'s `time` field,
  but it does **not** drive chart X-axes — those derive time from the
  configured Sample Time (`ChartConfigEditor` `xAxis.sampleTimeMs`), never
  from arrival or device timestamps (see
  [chartconfigeditor.h](../lib/dashboard/widgets/chartconfigeditor.h)).
  Required — a frame missing this group does not match the grammar.
- `<id>` — matched verbatim against `DashboardItem::key`
  ([dashboarditem.h](../lib/dashboard/dashboarditem.h)). Restricted to
  `[A-Za-z0-9_-]`, 1-64 characters (see "ID charset" below).
- `<payload>` — everything after the single space following `]`, up to
  (not including) `<EOL>`. Interpreted by the receiving widget according to
  its own `Format` config (CSV or Bytes — see "Payload encoding" below).
  Opaque to the envelope parser.
- `<EOL>` — `\n`, or `\r\n` (the `\r` is stripped before matching). A bare
  `\r` is **not** treated as a terminator — it has no special meaning and is
  just part of the accumulating line, since devices that emit lone `\r`
  mid-stream (e.g. a firmware `printf` typo) would otherwise silently
  fragment frames. The line assembler (Task 4) buffers raw bytes from
  `SerialManager::dataReceived` until it sees one of the two accepted
  terminators, since a frame can arrive split across multiple `readyRead`
  events.

Examples:

```
[1234][temp1] 23.5
[5820][rpm] 4200
[6001][ADC0] 4a3f
```

### Malformed lines

Any line that does not match the envelope grammar exactly (missing
brackets, non-numeric time, id outside the charset, no space before the
payload, empty id) is malformed:

- The terminal (`SerialTerminalWidget`) still shows it verbatim — it is a
  passive debug tap on the raw byte stream, not a filter (see Task 10).
- The router (Task 5) ignores it silently: `decodeFrame` returns `ok =
  false` and no widget receives anything. No error is surfaced to the
  status bar for a bad line — a busy port would otherwise spam it; only
  transport-level errors (`QSerialPort::errorOccurred`, Task 2) reach the
  status bar.

### ID charset

`[A-Za-z0-9_-]{1,64}` — no spaces, brackets, or control characters, which
keeps the envelope trivially unambiguous to parse (the id can't contain the
characters that delimit it) and keeps ids readable/typeable in the
terminal for manual testing.

Today `DashboardItem::key` has no charset restriction —
`PropertiesPanel::onKeyEditingFinished`
([propertiespanel.cpp](../lib/core/propertiespanel.cpp)) only trims
whitespace, and `DashboardGrid::isKeyAvailable` only checks uniqueness. A
key outside this charset is a perfectly valid app-internal identifier; it
is simply unreachable from the serial protocol (no frame `<id>` can ever
match it) until a future task adds a matching validator to the key field.
That validator is **not** part of this task — flagged here so Task 3 or a
follow-up doesn't reinvent the charset independently.

## Inbound: payload encoding

Both `ChartConfigEditor` and `GaugeConfigEditor` expose the same `Format`
choice, persisted as `"csv"` or `"bytes"`
([chartconfigeditor.cpp](../lib/dashboard/widgets/chartconfigeditor.cpp),
[gaugeconfigeditor.cpp](../lib/dashboard/widgets/gaugeconfigeditor.cpp)).
Both formats share the same slot structure — a frame carries `count` values
(the chart's `m_countSpin` field), addressed by a 0-based `index` per
consumer (gauge's `index` spin, chart series' `index` column) — so a device
firmware writes payloads the same way regardless of which format is
selected; only how each slot's text is decoded changes.

- **CSV** (`"csv"`): slots are ASCII, semicolon-separated, decimal (integer
  or float literal, `.` as decimal point). This matches the existing
  `ChartConfigEditor` header example `"1;2;3;4;5"` — semicolon, not comma,
  despite the UI label reading "CSV".

  ```
  [1234][accel] 0.12;-0.98;9.81
  ```

- **Bytes** (`"bytes"`): same semicolon-separated slot structure, but each
  slot is a hex-encoded little-endian raw value instead of a decimal
  literal, sized per that slot's configured `byteType`
  (`kByteTypeIds` = `uint8`/`int8`/`uint16`/`int16`/`uint32`/`int32`/
  `float32`/`float64`, duplicated today between `ChartConfigEditor` and
  `GaugeConfigEditor`). Lowercase hex digits, no `0x` prefix, no
  separators within a slot, always exactly `2 * sizeof(byteType)`
  characters (zero-padded) — e.g. `uint8` → 2 hex chars, `int16` → 4,
  `float32` → 8, `float64` → 16.

  ```
  [6001][ADC0] 4a3f;00c8
  ```
  (slot 0 = `0x3f4a` as `int16` little-endian = 16202; slot 1 = `0xc800` as
  `int16` little-endian = -14336, illustrating two different `byteType`s in
  one frame — decoding is per-slot, not per-frame.)

  Hex over Base64 because it stays human-legible and manually-typeable in
  the terminal for bench testing, needs no padding/alignment rules, and
  each byte round-trips to exactly 2 characters — useful when eyeballing a
  capture to debug a firmware encoding bug. Little-endian because it
  matches the native byte order of the microcontroller families this app
  targets (ARM Cortex-M, AVR, ESP32) — a firmware author on those platforms
  can memcpy a value into the frame without a byte-swap.

  If two widgets/series reference the same `index` with different
  `byteType`s, decoding disagrees between them; that is a user
  configuration mistake, not a protocol error, and is out of scope here
  (both simply decode "correctly" per their own declared type).

Delimited-text vs. Bytes only changes payload decoding; the envelope
grammar (time/id/EOL) above is identical either way.

## Outbound: control commands

`PushButtonWidget`/`ToggleSwitchWidget`/`SliderWidget` already capture the
literal command text per-widget in their config editors (`onPress`/
`onRelease`, `onCommand`/`offCommand`, `commandTemplate` — see
[controlconfigeditor.h](../lib/dashboard/widgets/controlconfigeditor.h)).
The wire format for outbound commands is:

```
<literal command text><line terminator>
```

- The command body is sent exactly as configured by the user — no envelope,
  no id, no brackets. It is not addressed to a widget on the device side;
  the app has no way (and no need) to know how the device's firmware
  demuxes it.
- **One placeholder is defined**: `{value}` — recognized only in
  `SliderConfigEditor::commandTemplate`, replaced with the slider's current
  value converted via `QString::number` at send time (matching the
  tooltip's documented intent, "Command sent on change — `{value}` is
  replaced with the current slider value"). No other config field
  (`onPress`/`onRelease`/`onCommand`/`offCommand`/long-press command)
  supports a placeholder — they are opaque literal strings, sent as-is.
  `{value}` is a plain substring replacement, not a format-string engine —
  no other tokens (`{time}`, `{id}`, etc.) are reserved by this spec.
- **Line terminator is a single global setting**, tied to the port
  connection (Run ribbon tab, Task 3), not per-widget — since it is a
  transport-level concern (what the device's line reader expects), not a
  per-command choice. Options: None, `\n` (LF), `\r` (CR), `\r\n` (CRLF).
  Default: `\n`, matching the inbound frame terminator so a device can use
  one line-oriented read loop for both directions.
- This terminator setting only applies to control-widget-triggered
  commands. It does **not** change `SerialTerminalWidget`'s existing
  keystroke behavior (Enter → raw `\r`, backspace → `0x7f`, Ctrl+letter →
  ASCII control code — see
  [serialterminalwidget.cpp](../lib/dashboard/widgets/serialterminalwidget.cpp)),
  which stays raw passthrough-per-keystroke; that is a deliberate, separate
  I/O mode (typing at a live terminal vs. firing a canned command), and
  changing it is out of scope for this task.

## Summary for implementers

| Concern | Decision |
|---|---|
| Frame envelope | `[<time>][<id>] <payload>` |
| Time field | Required, decimal ms, log/debug only — never drives chart X-axis |
| EOL (inbound) | `\n` or `\r\n`; bare `\r` is not a terminator |
| ID charset | `[A-Za-z0-9_-]{1,64}` |
| Malformed line | Shown raw in terminal, silently dropped by router |
| CSV payload | `;`-separated decimal literals, one per slot |
| Bytes payload | `;`-separated hex, little-endian, width = `2*sizeof(byteType)` per slot |
| Slot addressing | 0-based `index`, shared meaning across CSV/Bytes and across widget kinds |
| Outbound command | Literal configured text, `{value}` only in Slider's `commandTemplate` |
| Outbound terminator | Global port setting (Run tab), default `\n`, independent of terminal keystrokes |
