# Synthetic device

`tools/synthetic_device` is a standalone dev tool that plays the *device* side
of BTP v1 over a real serial port -- it exists so the serial telemetry
pipeline (`SerialManager` -> `BtpSession` -> `ProtocolRouter` ->
`ManifestClient`/`SubscriptionManager`/`TelemetryFieldRouter`, see
[PROTOCOL.md](PROTOCOL.md)) can be tested end to end without real hardware,
including with several independently-connected devices at once (see
[DEVICES.md](DEVICES.md)'s "Connections" section). Point a `Device` at it from
the Devices tab exactly like a real Bally_dongle and it drives the whole
handshake -- console `BTP/1 ENTER`/`READY`, `HELLO`/`HELLO_RESULT`, manifest
discovery, subscribe, live telemetry -- the same way real firmware would.

It links `traceview_protocol` (the same `BtpSession`/`ProtocolRouter` classes
TraceView's own client uses) and the same `btp::codec` library TraceView
fetches from `github.com/AlisonTristao/BTP` (root `CMakeLists.txt`), so it
tracks whatever BTP version/tag the app itself is pinned to automatically --
nothing here reimplements COBS/CRC/framing on its own.

## Architecture

`SyntheticDeviceSession` (`syntheticdevicesession.h`) is a generic BTP
device-role engine: console handshake, `HELLO`/`HELLO_RESULT`,
`MANIFEST_REQUEST`/`DATA` (built generically from a `QVector<TopicSpec>`),
`SUBSCRIBE`/`UNSUBSCRIBE`/`SESSION_CLOSE`, and the session watchdog -- all of
it identical no matter what's being simulated. A concrete **device profile**
subclasses it, supplying its topics/fields and a `sampleBody()` override that
encodes one topic's current sample. Two profiles exist today:

| Profile (`--profile`) | Class | Default `source_id` |
|---|---|---|
| `solar_panel` (default) | `SolarPanelDevice` | `0x53594E31` |
| `weather_station` | `WeatherStationDevice` | `0x57454131` |

Each simulated `source_name` also carries a flavor chip label
(`solar_panel (ESP32)`, `weather_station (ESP32-S3)`) -- there's no wire field
for firmware/chip identity in BTP v1 today (`HELLO` only negotiates protocol
capabilities), so `MANIFEST_DATA.source_name` is the closest real place to
reflect it.

Run **one process per profile/port** to simulate several distinct,
independently-connected devices side by side -- exactly what
`DeviceConnection` expects, since each `Device` in TraceView owns its own
`SerialManager`+`Backend` pair.

## What each profile exposes

### `solar_panel`

Simulates a solar/photovoltaic panel + battery installation, on two topics
driven off one shared, deterministic day/night cycle (a full cycle every 120
simulated seconds, sped up massively from a real 24h day so a chart actually
shows movement during a demo): irradiance rises and falls in a bell curve,
the panel heats up and generates current in proportion, and the battery
charges or drains depending on whether generation currently exceeds a fixed
simulated load -- every field moves coherently with the others rather than as
independent random walks.

| Topic | topic_id | Fields |
|---|---|---|
| `panel.environment` | `0x0001` | `temperature` (field 1, float32, `Cel`), `irradiance` (field 2, float32, `W/m2`), `humidity` (field 3, float32, `%`) |
| `panel.electrical` | `0x0002` | `panel_voltage` (field 1, float32, `V`), `panel_current` (field 2, float32, `A`), `panel_power` (field 3, float32, `W`), `battery_charge` (field 4, float32, `%`), `battery_voltage` (field 5, float32, `V`), `panel_status` (field 6, enum8: `idle`/`charging`/`full`/`discharging`) |

Battery charge is the one quantity that genuinely integrates over time (a
fixed 200 Wh capacity, a constant 40 W simulated load) rather than being a
pure function of elapsed time -- it keeps evolving in the background even if
nothing is subscribed to `panel.electrical`, the same way a real battery
would. Good fields to start with: `panel.environment`/`irradiance` (field 2)
or `panel.electrical`/`battery_charge` (field 4).

### `weather_station`

Simulates a weather station, on two topics driven off the *same style* of
day/night cycle the solar panel uses, but deliberately phase-shifted (23
simulated seconds later) and independently cloud-modulated -- representing a
station physically far from the panel, so `weather_station`'s irradiance and
humidity readings genuinely differ from `solar_panel`'s at any given moment,
the way two distant real installations would report different local weather.

| Topic | topic_id | Fields |
|---|---|---|
| `weather.environment` | `0x0001` | `irradiance` (field 1, float32, `W/m2`), `air_temperature` (field 2, float32, `Cel`), `air_humidity` (field 3, float32, `%`), `atmospheric_pressure` (field 4, float32, `hPa`) |
| `weather.ground` | `0x0002` | `soil_humidity` (field 1, float32, `%`), `wind_speed` (field 2, float32, `m/s`), `wind_direction` (field 3, float32, `deg`), `air_quality_index` (field 4, uint16, `1`), `rain_status` (field 5, enum8: `none`/`light`/`moderate`/`heavy`) |

Note `topic_id` `0x0001`/`0x0002` are reused between the two profiles --
that's fine, `topic_id` is local to each source's own namespace
(`TELEMETRY.md` section 2), and `source_id` is what actually distinguishes
`solar_panel` from `weather_station` on the wire.

**Not implemented by either profile** (deliberately out of scope for this
pass -- none of it blocks testing the serial telemetry path): `STATUS`,
`TERMINAL_IN`/`OUT`, `COMMAND`/`COMMAND_REQUEST`, any non-serial transport.

## Running it

```
synthetic_device --port <name> [--baud 921600] [--profile solar_panel] [--source-id 0x...]
```

- `--port` is required -- the OS-level serial port name (e.g. `COM6`).
- `--baud` defaults to 921600, matching the real Bally_dongle's Serial USB
  shell baud ([ECOSYSTEM.md](ECOSYSTEM.md)).
- `--profile` selects `solar_panel` (default) or `weather_station`.
- `--source-id` accepts hex (`0x...`) or decimal, must be non-zero; defaults
  to the selected profile's own default (see the table above).

To simulate both devices at once, run two instances against two different
ports:

```
synthetic_device --port COM10 --profile solar_panel
synthetic_device --port COM12 --profile weather_station
```

Each opens its port once at startup and fails fast if that doesn't work --
run it when you're ready to test, there's no ambient retry loop like
`DeviceConnection`'s. stdout logs every handshake/manifest/subscribe event as
it happens, plus a 1-second heartbeat per actively-subscribed topic (not
per-sample, so a fast topic doesn't flood the terminal).

## Testing it against TraceView locally

Two separate processes can't open the same physical COM port, so testing
`synthetic_device` against a running TraceView needs an actual paired serial
connection *per simulated device*:

- A null-modem emulator (e.g. [com0com](https://com0com.sourceforge.net/) on
  Windows) creating a linked virtual COM pair, or
- Two physical UART adapters wired RX/TX/GND together.

For two devices at once, that's two separate pairs (four virtual/physical
ports total). Run `synthetic_device --port <one end of a pair> --profile ...`
for each, then add one `Device` per pair in TraceView's Devices tab, each
pointed at the *other* end. The Devices tab's connection dot only reflects the
port being open, not the BTP handshake succeeding -- watch each
`synthetic_device`'s own stdout for `HELLO_RESULT`/`MANIFEST_DATA`/
`SUBSCRIBE_RESULT` lines, and a chart widget actually plotting the moving
`.../irradiance` curve for each device, to confirm the full chain is working
for both independently.
