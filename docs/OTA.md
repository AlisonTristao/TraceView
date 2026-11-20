# OTA Update tab

Sending a new firmware to a device over Wi-Fi, from inside TraceView, instead
of plugging a cable in and running `pio run -t upload`.

The devices are [TraceKit](https://github.com/AlisonTristao/TraceKit)
devices. A TraceKit firmware lives in the `factory` partition, next to a small
**recovery** firmware in `ota_0`. The recovery is what receives the upload: the
application itself never writes its own partition. The device restarts into it
on `btp -recovery`, after crashing three times in a row, or when the
application is missing; it joins the same Wi-Fi as the application, answers at
the same `<name>.local`, and goes back to the application by itself after a
good upload (or after 10 minutes without one).

This is the one part of the app that does **not** speak BTP: the recovery is a
plain HTTP server (`GET /status`, `POST /update`, `POST /reboot`). A firmware
image is far too large to fragment through a radio datagram, and a device in
the middle of a flash has no business holding a telemetry session.

Open it with **File → Upload Firmware (OTA)…**. There is never more than one:
the action re-focuses the existing tab (`MainWindow::onOpenOtaTab`).

## The table

One row per `Device` in the project.

| Column | What it is |
|---|---|
| Name | `Device::name`. |
| Address | Where the recovery answers: `otaAddressFor()` ([otaclient.h](../lib/ota/otaclient.h)). |
| State | **In recovery**, **Running the app** (connected, the recovery is not up), **Offline**, or **Writing…** while an upload is being written. Hover for why it is in the recovery, or why it is unreachable. |
| Application | In the recovery: the application in the app partition (`project version`). Running: the `fw_version` the application reported. |
| Recovery | The recovery's own version; hover for the chip and the largest image it takes. |
| (actions) | **Upload…** and **To app** (leave the recovery without uploading). |

### The address

Nobody has to type it. `otaAddressFor()` takes, in order:

1. `Device::otaAddress`, when the user set one in Device Settings (a proxy, a
   fixed IP);
2. the `ota_endpoint` the device announced in its manifest (`source_info`) —
   TraceKit announces `<name>.local`;
3. the TCP host the device is connected through.

The status poll runs at 1 Hz and only while the tab is visible
(`OtaTab::setActive`), plus while an upload waits for its device to reach the
recovery. A tick landing while a row's request is still running is skipped
rather than restarting it (`OtaClient::statusRequestInFlight`), so a device
slower than the poll interval still resolves — restarting instead left slow
rows on "Checking…" forever.

## Uploading

**Upload…** works from both states:

- **In recovery:** the image goes straight up.
- **Running the app, connected:** after a confirmation, TraceView sends
  `btp -recovery` over the open session (`OtaTab::recoveryRequested`), waits up
  to 90 s for the recovery to answer, and uploads then. One click from the old
  firmware to the new one.

Not connected and not in the recovery, there is nothing to restart it with:
the tab says so.

Before sending, `OtaClient` checks that the file starts with the ESP image
magic byte (`0xE9`, so a merged or bootloader `.bin` is refused here, not by
the device) and that it fits the partition (`max_size` in `/status`). Uploads
get no transfer timeout; the progress bar shows they are still moving. The
recovery's reply ("OK. Rebooting into sensor 1.3.0.") goes to the status bar;
a refusal opens a warning with the device's own reason.

## Authentication

Every POST is signed with **key E**, the AES key TraceView already derives for
the device's BTP session from its password (`Device::peerPassword`,
PBKDF2 in `protocol/keyderivation.h`). There is no separate OTA password.

```
nonce      = GET /status -> "nonce"            (16 octets, new after every attempt)
message    = "btp-ota-1" || nonce || action || sha256(image)     ('U' update, 'B' reboot)
X-OTA-Auth = hex(HMAC-SHA256(key_E, message))
```

The password never leaves this machine, and a captured request cannot be
replayed. When the device has no password stored, the tab asks for it once and
keeps it in memory for the session. A recovery built with
`CONFIG_TRACEKIT_RECOVERY_INSECURE` (development only) reports `"auth":"none"`
and is not asked for one.

[otaauth.h](../lib/ota/otaauth.h) implements it; `tests/test_otaauth.cpp`
checks it against the same golden vectors as TraceKit's firmware, its Python
tool and its web page, and `tests/test_otaclient.cpp` runs whole uploads
against a fake recovery.

## mDNS

A `*.local` address is resolved by `MdnsResolver`
([mdnsresolver.h](../lib/ota/mdnsresolver.h)) — our own multicast query on
224.0.0.251:5353 — before `QNetworkAccessManager` sees it. Qt would otherwise
hand the name to the OS resolver, and on Windows without Bonjour that path
simply fails.

If our query gets no answer within 3 seconds, the request falls back to the
plain hostname for `getaddrinfo()`. Resolved addresses are cached for 60
seconds.

## Related

- [DEVICES.md](DEVICES.md): the device model and the BTP transports this side
  channel sits alongside.
- TraceKit's `docs/PLANO.md`, section 4.14: the recovery itself.
