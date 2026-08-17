# Ecosystem compatibility

TraceView is the visualization endpoint of the BTP telemetry pipeline. This
document records how the four repositories fit together and what TraceView
needs to consume so the pieces stay compatible as each project evolves
independently.

## Pipeline

```
bally_robot (hardware)
      |
      v
Bally_OS (ESP32-S3 firmware)
   - Logger module: circular PSRAM buffer
   - transmits via ESP-NOW (primary) or Serial (fallback)
      |
      v
Bally_dongle (LilyGO T-Dongle-S3 firmware)
   - EspNowManager receives robot telemetry/logs
   - exposes an interactive TinyShell over Serial USB (921600 baud)
   - persists history to SQLite on SD
      |
      v
TraceView (this repository)
   - connects over Serial (QSerialPort) to the dongle
   - parses telemetry frames
   - renders live plots / state machine view / log stream
```

## Compatibility contract

TraceView must not assume internal implementation details of the other
projects beyond a defined wire format. As of this writing, that wire format
(the exact framing/encoding `Bally_OS`'s `Logger` uses, and how
`Bally_dongle`'s shell/`EspNowManager` re-exposes it over Serial) is not
yet formalized as a spec — it lives implicitly in each project's source.

**Before implementing telemetry ingestion, the first cross-repo task is to
write down that protocol explicitly** (message framing, field layout,
timestamps, command/log distinction) as a shared spec, ideally checked into
all four repositories or a dedicated `BTP` reference. Until then,
protocol-facing code in TraceView should stay isolated behind a single
interface (e.g. a `TelemetrySource` abstraction) so the transport/parsing
layer can be swapped without touching the UI.

## Baud rate / transport reference

- Dongle Serial USB shell: `921600` baud (from `Bally_dongle`).
- ESP-NOW is used robot → dongle; TraceView never talks ESP-NOW directly, it
  only sees whatever the dongle re-exposes over Serial.

## Project conventions kept consistent across the ecosystem

- C++, PlatformIO-style `include/` + `lib/` + `src/` layout on the firmware
  side; TraceView mirrors `include/` + `lib/` + `src/` on the desktop side.
- MIT license (matches `TinyShell` and other shared libraries).
