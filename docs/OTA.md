# OTA Update tab

Pushing a firmware binary to a robot over Wi-Fi, from inside TraceView,
instead of plugging a cable in and running `pio run -t upload`.

This is the one part of the app that does **not** speak BTP. bally_OS's
`lib/OTAUpdater` exposes a plain HTTP server on the robot — `GET /status`
and `POST /update` — over a separate Wi-Fi link from the
Serial/USB-HID/hub transport everything else here talks over. That
separation is deliberate on the firmware side: a robot that is mid-flash
has no business also trying to hold a telemetry session together, and a
firmware image is far too large to fragment through a radio datagram.

Open it with **File → Upload Firmware (OTA)…**. Unlike log tabs, there is
never more than one: the action re-focuses the existing tab if it is
already open (`MainWindow::onOpenOtaTab`).

## The table

One row per `Device` in the project — every device, not just the ones with
an address, so a device missing its OTA config is visible as missing rather
than absent.

| Column | What it is |
|---|---|
| Name | `Device::name`. |
| OTA Address | `Device::otaAddress` — a bare IP, a hostname, or an mDNS `*.local` name. Edited in Device Settings, not here. |
| OTA Online | Reachability, polled while this tab is visible. Hover it for *why* when it is offline. |
| Firmware | What the robot reports it is currently running. Blanked when unreachable — this column answers "what is on the device", and an unreachable device has no answer. |
| Password | `X-OTA-Password` for the upload. Typed here; only saved into the project if "Remember" is ticked. |
| Upload | Opens a file picker, then swaps in place for a progress bar. |

The status poll runs at 1 Hz, and **only while the tab is the visible one**
(`OtaTab::setActive`, driven from `MainWindow::onRibbonTabChanged`) — a
background tab must not keep generating HTTP traffic against every robot in
the project.

That interval is deliberately shorter than the 4-second timeout a single
status request is allowed. A tick landing while a row's request is still
running is skipped rather than restarting it
(`OtaClient::statusRequestInFlight`), so a device slower than the poll
interval still resolves. Restarting instead is exactly the bug this
replaced: every attempt was cancelled by the next tick and the row sat on
"Checking…" forever — worst for the case most worth reporting, a host that
is up but slow to answer.

## Passwords

Three-state on purpose:

- **Typed, not remembered** — kept in `OtaTab`'s session map, survives a
  device-list rebuild (keyed by `Device::id`, not row), dies with the app.
- **Remembered** — ticking "Remember" emits `passwordCacheChanged`, which
  `MainWindow` turns into a real `DevicesGrid::updateDevice()`, so it lands
  on the undo stack and in `.tvproj` like any other edit. `OtaTab` has no
  undo stack of its own; this is the single point where it commits a
  project mutation.
- **Empty** — fine. The robot only checks the header if it has a password
  configured.

A successful upload with "Remember" already ticked re-emits, so a password
corrected *after* ticking the box is the one that gets saved.

## mDNS

A `*.local` address is resolved by `MdnsResolver`
([lib/ota/mdnsresolver.h](../lib/ota/mdnsresolver.h)) — our own multicast
query on 224.0.0.251:5353 — before either request type touches
`QNetworkAccessManager`. Qt would otherwise hand the name to the OS
resolver, and on Windows without Bonjour installed that path simply fails,
which is the single most common reason `ballyrobot.local` "doesn't work".

The socket binds with `ShareAddress | ReuseAddressHint`: the point is to
coexist with a real responder already on 5353 (Bonjour, or another instance
of this app), not to fight it for the port.

If our own query gets no answer within 3 seconds, the request falls back to
the plain hostname and lets `QNetworkAccessManager` try `getaddrinfo()`
exactly as it would have before this resolver existed — covering a network
that filters multicast, or a firewall rule specific to this socket. Only if
*that* also fails does the row report unreachable, and the reason lands in
the status cell's tooltip: "Host not found" and "Connection refused" are
very different problems, and "Offline" alone distinguishes neither.

Resolved addresses are cached for 60 seconds, so a 1 Hz poll does not
re-query the network on every tick, while a robot picking up a new DHCP
lease is still noticed well within a working session.

## Uploading

`POST /update` with the file as the **raw body** — no multipart, matching
`OTAUpdater::handle_update_post`. Uploads get no transfer timeout: a
firmware image legitimately takes longer than a status check, and the
progress bar already shows it is still moving.

The robot sends a readable body with both success (200, "OK. Reset the
robot…") and its own deliberate failures — 401 wrong password, 400 empty
body, 500 no OTA partition or write failed — so that text is what the user
is shown. A transport-level failure has no body, and falls back to
`QNetworkReply::errorString()`.

## Related

- [DEVICES.md](DEVICES.md) — where `otaAddress`/`otaPassword` are edited,
  and the BTP transports this side channel sits alongside.
- [ECOSYSTEM.md](ECOSYSTEM.md) — why the protocol/transport boundary is kept
  out of UI code across the Bally repositories.
