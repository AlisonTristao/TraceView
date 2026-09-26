#pragma once

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "telemetry/deviceinforecord.h"

namespace traceview {

// Which communication/transport protocol a device speaks. Only BTP exists
// today (see lib/protocol), but this stays an enum class (rather than a bare
// string) so a second protocol can be added later -- one new enumerator --
// with no API break for anything that already stores/compares a CommType
// value.
//
// There is deliberately no commTypeLabel() display-string helper: with a
// single enumerator, every place that tried to print one (the card body, the
// config dialog) just showed the bare word "BTP" next to something else
// already saying it. drawCommTypeIcon() (devicecard.cpp) is the one thing
// that still switches on this, and it draws a glyph rather than text. Add a
// label helper back when there is a second protocol to tell apart, not
// before.
enum class CommType { Btp };

// Which physical link a device's connection uses -- independent of CommType
// above (the protocol): BTP can run over either (see BTP's "usb_hid"
// transport profile in docs/fragmentation-and-transports.md section 3.3,
// and lib/core/deviceconnection.h, which is
// the actual switch point). traceview_devices still doesn't depend on
// QSerialPort/hidapi (see lib/CMakeLists.txt) -- this is just a plain enum
// selecting which of Device::portName/usbPath below DeviceConnection should
// use.
// HubChannel is the odd one out and deliberately so: it is not a physical
// link at all, but a channel multiplexed over ANOTHER device's connection --
// a robot reached through the dongle it sits behind (see core/hubtransport.h).
// It lives in the same enum because everything above this layer asks the same
// question of it as of the other transports ("which transport does this
// device use"), and DeviceConnection remains the single switch point.
// Existing values are explicit: project files and other consumers already
// persist these ordinals, so new transports must only be appended.
enum class TransportType {
    Serial = 0,
    UsbHid = 1,
    HubChannel = 2,
    Tcp = 3,
    Ble = 4,
};

inline QString transportTypeLabel(TransportType type) {
    switch (type) {
        case TransportType::Serial:
            return QCoreApplication::translate("Device", "Serial");
        case TransportType::UsbHid:
            return QCoreApplication::translate("Device", "USB");
        case TransportType::HubChannel:
            return QCoreApplication::translate("Device", "Hub");
        case TransportType::Tcp:
            return QCoreApplication::translate("Device", "TCP");
        case TransportType::Ble:
            return QCoreApplication::translate("Device", "BLE");
    }
    return QString();
}

// One USB HID device DeviceConfigDialog's picker can offer -- `path` is
// hidapi's own device path (what UsbHidManager::open() expects, not meant
// to be typed by a human), `label` is what the combo box shows instead.
// Lives here (not core/usbhidmanager.h) so DeviceConfigDialog can accept it
// without traceview_devices depending on hidapi -- MainWindow converts
// UsbHidManager::DeviceInfo to this the same way it already converts
// QSerialPortInfo to a bare QStringList for the port picker.
struct UsbDeviceOption {
    QString path;
    QString label;
};

// One entry in DeviceConfigDialog's serial port picker. `name` is what gets
// stored in Device::portName and handed to SerialTransport::open(); `label`
// is only what the combo shows. On desktop the two are the same OS port name
// ("COM3", "ttyACM0"); on Android `name` is a stable "usb:VVVV:PPPP[:serial]"
// key (the OS device path changes on every replug, see
// core/androidusbserialtransport.h) and `label` the product name the user
// recognizes. Same reason as UsbDeviceOption above for living here.
struct SerialPortOption {
    QString name;
    QString label;
    // The product name the USB device behind this port reports, empty when
    // none (or not a USB port). A bally_OS robot reports its configured
    // identity name here -- what resolveLinkByName() matches a Serial link
    // set to "automatic" against.
    QString productName;
};

// What the port picker shows for one port: "COM5 — BallyRobot" when the
// USB device behind it reports a product name (a bally_OS robot reports its
// configured identity name there, see USBMassStorage::set_product_name()),
// else just the port name. `productName` is whatever the platform gave --
// SerialManager::portProductNames() -- and is dropped when it adds nothing
// (empty, "n/a", or the port name itself).
QString serialPortLabel(const QString& portName, const QString& productName);

// One hub.peers entry, decoded from the dongle's own live telemetry -- see
// MainWindow::hubPeersFor()/onHubPeerFieldSample() (core/mainwindow.cpp).
// traceview_devices never decodes BTP itself; this arrives as an
// already-reduced value type, same reasoning as UsbDeviceOption above.
//
// `channel` is a DISPLAY INDEX ONLY. The dongle assigns it in the order it
// first hears each peer, and it is not stable across a dongle reboot (see
// bally_dongle's DonglePublisher.h, PeerRecord). DeviceConfigDialog shows it
// as a convenience label; only `sourceId` may ever be written into
// Device::peerSourceId.
struct HubPeer {
    quint32 sourceId = 0;
    quint8 channel = 0;
    QString mac;  // "AA:BB:CC:DD:EE:FF", display only
    // The robot's configured name (its manifest source_info "name"), as the
    // dongle's cached copy of that manifest reported it. Display only, like
    // `mac` -- the whole point of showing it is telling robots apart in the
    // picker, but what gets stored is still `sourceId`. Empty until that
    // manifest has been read (or when the robot has no name set).
    QString name;
    quint32 lastSeenAgeMs = 0;
    bool online = false;
    // The boot this robot is currently on, per the dongle's live view. A
    // change here (same sourceId, new bootId) means the robot rebooted -- what
    // MainWindow watches to make a hub child re-request its catalog and
    // re-subscribe without the operator reconnecting it by hand.
    quint32 bootId = 0;
    // RSSI (dBm) of the last authenticated frame the dongle heard from this
    // peer, from its own promiscuous-mode sniffer (see bally_dongle's
    // EspNowManager). 0 before any sample has arrived.
    qint8 rssi = 0;
    // Round-trip time (ms) of the last completed ping probe the dongle sent
    // this peer (bally_dongle's BtpTransport::notePingSent/notePingReply).
    // 0 before the first one completes.
    quint32 rttMs = 0;
};

// The default baud rate for a new Serial link: the ESP32-S3's top UART rate
// (bally_OS's BAUDRATE). A robot on its native USB CDC port ignores the baud
// rate entirely, so this only matters for a board behind a USB-UART bridge.
inline constexpr qint32 kDefaultSerialBaudRate = 5000000;

// One way of reaching a device -- a serial port, a USB HID path, a TCP host,
// a BLE address, or a hub channel (a robot behind a dongle). A Device's own
// transport fields ARE its primary link; Device::extraLinks holds the others,
// in the order MainWindow tries them (DeviceLinkCycler below) whenever the one
// in use is not live. Same field names and meaning as the matching Device
// fields -- see those for what each one holds.
struct DeviceLink {
    TransportType transportType = TransportType::Serial;
    QString portName;
    qint32 baudRate = kDefaultSerialBaudRate;
    QString usbPath;
    QString tcpHost;
    quint16 tcpPort = 44300;
    QString bleAddress;
    QString parentDeviceId;
    quint32 peerSourceId = 0;
    // Unticked in the connections table: kept, but skipped by the cycler.
    bool enabled = true;
    // "Automatic": the target is derived from Device::robotName each time the
    // link is dialed (resolveLinkByName()) instead of the fields above --
    // Serial finds the port whose USB device reports that name, TCP dials
    // <name>.local, BLE scans for that advertised name, a hub channel finds
    // the robot of that name among the hubs' peers. Nothing resolved is ever
    // written back, so the saved project keeps saying "automatic".
    bool autoTarget = false;
};

// One connected/known device, shown as a single card in the Devices panel
// (see devicesgrid.h), and (as of the multi-device connection refactor) the
// config for one real, independent serial connection -- see
// core/deviceconnection.h, which owns the actual SerialManager/Backend pair
// this data feeds. Kept as plain public fields (this codebase's
// low-ceremony value-type style, see DashboardItem in
// dashboard/dashboarditem.h) with no behavior tying it to any particular
// data source.
struct Device {
    QString id;  // stable identity; auto-generated by
                 // DevicesGrid::addDevice() if left empty
    QString name;
    // Live-mirrored from DeviceConnection::isConnected() by whoever owns the
    // connection (MainWindow) -- not user-editable once a real connection
    // exists. Empty portName means "not configured yet," so this stays
    // false regardless of connection attempts.
    bool connected = false;
    CommType commType = CommType::Btp;
    QString description;
    // JavaScript run live for this device by its own DiagramScriptRuntime
    // (lib/diagram/diagramscriptruntime.h): onTelemetry(sample)/
    // onTerminal(text) react to this device's real traffic, and
    // device.sendCommand()/sendTerminal() write back to it. Edited via the
    // script icon on this device's card (DeviceCard), not this dialog --
    // plain persisted config, same treatment as `description`.
    QString script;
    // What the device reported in its own HELLO_RESULT (protocol/
    // btphandshake.h's sessionEstablished()), surfaced read-only in
    // DeviceConfigDialog's "Reported by device" section -- never
    // user-editable. Live session state, same treatment as `connected`
    // above: reset to empty on disconnect (MainWindow::
    // onDeviceConnectionStateChanged()) and not persisted (deviceToJson()
    // below), since a stale value from a previous device on this port would
    // otherwise linger in a saved project.
    QString btpVersion;
    QString btpId;

    // The device's MANIFEST_DATA source_info block (BTP's docs/commands.md
    // section 3.12): firmware version, chip, running partition, a configured
    // name/description -- shown read-only in DeviceConfigDialog's "Reported by
    // device" section. Same live-session treatment as btpVersion above: filled
    // from Backend::deviceInfoReported, reset to empty on disconnect
    // (MainWindow::onDeviceConnectionStateChanged()), and NOT persisted
    // (deviceToJson()) -- a value from a previous device on this port must not
    // linger in a saved project.
    QVector<DeviceInfoRecord> reportedInfo;

    // Which physical link this device connects over -- see TransportType
    // above. Selects which of the fields below DeviceConnection actually
    // uses; the other stays around unused (harmless, same "keep whatever
    // was there" treatment portName/baudRate/lineTerminator already get
    // when a device isn't configured at all).
    TransportType transportType = TransportType::Serial;

    // Real transport config for this device's connection. Empty portName
    // means "not configured" -- DeviceConnection then never attempts to
    // open a port. traceview_devices can't depend on traceview_ui's
    // QSerialPort-based SerialManager (see lib/CMakeLists.txt layering), so
    // baudRate/portName are plain types here rather than QSerialPort ones.
    QString portName;
    qint32 baudRate = kDefaultSerialBaudRate;
    // Ordinal mirroring traceview::LineTerminator (core/serialtransport.h:
    // None=0, Lf=1, Cr=2, CrLf=3) -- kept as a plain int for the same reason
    // portName/baudRate are plain types, rather than depending on that enum
    // directly. Only meaningful for TransportType::Serial (control-widget
    // commands are a raw-text-over-the-console-byte-stream concept that has
    // no equivalent over TransportType::UsbHid, which has no console at
    // all -- see BTP's docs/fragmentation-and-transports.md section 3.3).
    int lineTerminator = 1;  // Lf, matching SerialManager's own default

    // Target for TransportType::UsbHid connections: the hidapi device path
    // (see UsbDeviceOption::path above), analogous to portName for
    // TransportType::Serial. Empty means "not configured," same convention
    // as an empty portName -- DeviceConnection then never attempts to open
    // it.
    QString usbPath;

    // --- Direct TCP -------------------------------------------------------
    // Hostname or numeric address of the ESP32-S3 TCP server. This is kept
    // separate from portName/baudRate because TCP has no serial settings.
    QString tcpHost;
    quint16 tcpPort = 44300;

    // --- Direct BLE -------------------------------------------------------
    // Discovery hint used to dial the connection, in one of two forms:
    //   - the robot's advertised NAME ("BallyRobot"), the BLE counterpart of
    //     a TCP hostname -- BleTransport scans for it on every attempt, so it
    //     survives an address change and means the same thing on every host
    //     OS (macOS/iOS never expose a MAC, only a per-host UUID);
    //   - a platform address: a MAC on most backends, an opaque per-host UUID
    //     on some (see QBluetoothDeviceInfo::address()/deviceUuid()).
    // Neither is identity -- a name is whatever the robot is configured to
    // advertise, and an address can change across a firmware update or a
    // different host OS. BleTransport::open() takes exactly this string
    // (BleTransport::isPlatformAddress() tells the two forms apart); nothing
    // below the transport ever reads it as meaning anything.
    QString bleAddress;
    // Stable identity learned from HELLO_RESULT, when known. A platform BLE
    // address or display name is only a discovery hint and is intentionally
    // not represented as the device identity here.
    QString blePeerUuid;

    // --- TransportType::HubChannel only -----------------------------------
    // A device reached THROUGH another device: a robot behind the dongle it
    // talks to. Ignored entirely by the other two transports, same "keep
    // whatever was there" treatment portName/usbPath already get.

    // Device::id of the hub this one rides. Empty means "not configured".
    QString parentDeviceId;

    // The robot's BTP source_id, and the ONLY thing that may be persisted as
    // its address.
    //
    // ============================ READ THIS ==============================
    // The dongle also publishes a "channel" number per peer (hub.peers), and
    // it is tempting to store that instead because it is short and it is what
    // the UI shows. Do not. That number is a display index assigned in the
    // order the dongle first heard each peer, and it is not stable across a
    // dongle reboot: bring the robots up in a different order and channel 1
    // now names a different one. A project saved with the index would reopen
    // pointing at the wrong robot -- plotting real data, from the wrong
    // machine, raising no error anywhere. A source_id is the robot's own
    // identity and does not move.
    // =====================================================================
    //
    // Zero means "not configured", the safe default a project file missing
    // the field falls back to, and it never connects.
    quint32 peerSourceId = 0;

    // Live-mirrored by MainWindow::reconcileHubChildPresence while this child is
    // connected -- the same "session state, not configuration" treatment as
    // `connected`/`btpVersion`: not user-editable, not persisted (deviceToJson()),
    // and reset to the defaults below on disconnect.
    //
    // What deviceLinkState() reads for a hub child, and pessimistic by default
    // on purpose. "Connected" for a hub child only means the dongle is relaying
    // this child's frames -- it says nothing about the robot, which can be off,
    // out of range, or unkeyed while the hub link is perfectly fine. Green is
    // therefore earned, not assumed:
    //
    //   peerOnline         the robot's own data frames are reaching this
    //                      TraceView end to end right now -- a channel-B frame
    //                      that passed AEAD open (the signal the operator can
    //                      corroborate in the BTP monitor), or, until a
    //                      subscription is producing telemetry, the dongle's
    //                      hub.peers online=1 as a fallback.
    //   peerPresenceKnown  reconcile has a verdict either way. False right
    //                      after connecting and while it has neither robot
    //                      data nor a readable hub.peers view -- the card
    //                      shows amber "locating", never green, until it flips.
    //
    // `peerBootId` lets MainWindow notice a robot reboot (from hub.peers).
    bool peerOnline = false;
    bool peerPresenceKnown = false;
    quint32 peerBootId = 0;

    // RSSI (dBm) and RTT (ms) of the dongle<->robot ESP-NOW link, mirrored
    // from the parent's hub.peers watch by the same
    // MainWindow::reconcileHubChildPresence loop that sets peerOnline/
    // peerBootId above -- same live-session, not-persisted treatment. 0
    // (HubPeer's own "never observed" default) until the first sample.
    qint8 peerRssi = 0;
    quint32 peerRttMs = 0;

    // Password for this robot's endpoint key (channel B). Live session input,
    // not configuration -- see cachePeerPassword for whether it is persisted.
    QString peerPassword;

    // Whether peerPassword goes into the saved project.
    //
    // Default false, and that is the important half: a .tvproj is a file
    // people mail to each other and commit, so it must not become a secrets
    // file by accident. Opting in is per device and explicit, for the case
    // where a project lives on one machine and retyping every password on
    // every open is friction with no security bought.
    bool cachePeerPassword = false;

    // --- OTA firmware upload (see lib/ota) -------------------------------
    // A separate Wi-Fi/HTTP side channel bally_OS serves from its DEBUG
    // state (OTAUpdater), independent of whichever transport above this
    // device actually connects through -- a device can be configured for
    // Serial/USB/Hub *and* have an OTA address, since OTA doesn't run over
    // BTP at all.

    // mDNS hostname ("robot1.local") or bare IP the OTA tab targets for this
    // device's GET /status and POST /update. Empty means "not configured for
    // OTA" -- the OTA tab still lists the device, just with nothing to poll.
    // Not a secret, so unlike peerSourceId this is always persisted.
    QString otaAddress;

    // Password for this device's X-OTA-Password header. Live session input,
    // not configuration -- see cacheOtaPassword for whether it is persisted.
    QString otaPassword;

    // Whether otaPassword goes into the saved project. Same opt-in-only
    // convention as cachePeerPassword above, and for the same reason.
    bool cacheOtaPassword = false;

    // --- Several ways to connect ------------------------------------------
    // Alternatives to the primary link above (its transportType/portName/...
    // fields), tried in this order after it by MainWindow's DeviceLinkCycler
    // whenever the link in use is not live. The channel-B password
    // (peerPassword) is the device's, shared by every link -- it is the
    // robot's key, not the link's. Persisted.
    QVector<DeviceLink> extraLinks;
    // Which link is being used right now: 0 = the primary fields above,
    // i > 0 = extraLinks[i - 1]. Live state, like `connected`: set by
    // MainWindow as it cycles, never persisted, 0 on load. effectiveDevice()
    // folds it back into the flat fields for everything that reads them.
    int activeLink = 0;

    // --- One name for every link -------------------------------------------
    // The robot's own name -- the identity name bally_OS reports in its
    // manifest, advertises over BLE, and puts in its USB descriptor. Links
    // set to automatic (DeviceLink::autoTarget) are reached through it, so a
    // robot is configured by typing one name and ticking the links it has.
    // Persisted; empty = no automatic link can resolve.
    QString robotName;
    // DeviceLink::autoTarget for the primary link (the fields above).
    bool autoTarget = false;
};

// Link 0 is the device's own transport fields, 1..N its extraLinks.
int deviceLinkCount(const Device& device);
DeviceLink deviceLinkAt(const Device& device, int index);
// `device` with `link` copied over its transport fields (everything else --
// identity, password, live state -- unchanged).
Device deviceWithLink(const Device& device, const DeviceLink& link);
// `device` as it is actually connected right now: its active link's
// transport fields in place of the primary ones. The same Device when the
// primary link is in use (or activeLink is out of range).
Device effectiveDevice(const Device& device);
// Whether `link` names something to dial at all (a port, a host, an
// address, a parent + robot) -- an empty row in the table is skipped.
bool deviceLinkConfigured(const DeviceLink& link);
// Whether `a` and `b` dial the same thing (transport and target fields;
// baud rate and the enabled/automatic flags aside).
bool sameLinkTarget(const DeviceLink& a, const DeviceLink& b);
// Whether the cycler may try `link` of `device`: enabled, and either
// configured by hand or automatic with a robot name to resolve.
bool deviceLinkUsable(const Device& device, const DeviceLink& link);

// "Robo 2" -> "robo-2": the mDNS host label a robot of that name answers to
// (lowercase letters, digits and '-', at most 63). Empty when nothing is left.
QString mdnsHostFromName(const QString& name);

// What an automatic link can be resolved against, gathered by MainWindow
// from the live world: the serial ports (with their USB product names) and
// every robot each connected hub currently reports.
struct HubPeerSighting {
    QString parentDeviceId;
    HubPeer peer;
};
struct LinkDirectory {
    QVector<SerialPortOption> ports;
    QVector<HubPeerSighting> hubPeers;
};

// `link` with its target filled in from `robotName` when it is automatic
// (see DeviceLink::autoTarget); returned unchanged otherwise. A target that
// cannot be pinned down -- two ports report the name; no hub has that
// robot, or two robots share the name -- comes back empty, which dials
// nothing: guessing would connect to the wrong machine. A Serial link that
// no port names falls back to the port it was last set to, when present.
DeviceLink resolveLinkByName(const QString& robotName, const DeviceLink& link,
                             const LinkDirectory& directory);
// Whether resolving `link` needs the directory at all (a Serial or hub
// channel link set to automatic).
bool linkNeedsDirectory(const DeviceLink& link);
// Every link's configuration (primary included), as one comparable blob --
// what changes when the user edits HOW to reach the device, and nothing else
// (not live state, not the name, not the password).
QByteArray deviceLinksSignature(const Device& device);
// "TCP 192.168.0.50:44300", "Serial COM5 @ 921600", "Hub 0x1A2B3C4D" --
// one line for the connections table, the device card and status messages.
// With `robotName`, an automatic link names what it resolves to by name.
QString deviceLinkSummary(const DeviceLink& link, const QString& robotName = QString());

// Decides, once per tick, which of a device's links MainWindow should be
// dialing. Deliberately pure (no timers, no DeviceConnection) so the rule is
// testable on its own (tests/test_device.cpp):
//
//   - while the link in use is live, or the user does not want this device
//     connected, nothing moves (and the attempt clock restarts);
//   - otherwise, once the link in use has had kAttemptMs to come up and has
//     not, move on to the next usable link, wrapping around to the first.
//
// No preemption: a live fallback link is kept even when the primary would
// be available again -- switching would drop a working session for a guess.
class DeviceLinkCycler {
public:
    // Long enough for a BLE connect + GATT discovery + HELLO, and for a
    // serial port's HELLO retries, to finish on a link that works.
    static constexpr qint64 kAttemptMs = 10000;
    // An automatic link whose name resolved to nothing (no port reports it,
    // no hub has the robot) has nothing to dial: it only gets a couple of
    // looks before the next link's turn.
    static constexpr qint64 kUnresolvedAttemptMs = 2000;
    // A link that already reported a failure this attempt (mDNS did not find
    // the host, the BLE scan did not see the robot, the port would not open)
    // is not going to come up by waiting out kAttemptMs: the next link gets
    // its turn this soon after the attempt began.
    static constexpr qint64 kFailedAttemptMs = 1000;

    // `usable[i]`: link i can be tried (deviceLinkUsable). `unresolved`: the
    // link in use is automatic and found no target this time.
    // Returns the link to switch to, or -1 to stay on active().
    int tick(const QVector<bool>& usable, bool wanted, bool live, qint64 nowMs,
             bool unresolved = false) {
        if (!wanted || live || m_attemptStartMs < 0) {
            m_attemptStartMs = nowMs;
            m_failed = false;
            return -1;
        }
        const qint64 window =
            unresolved ? kUnresolvedAttemptMs : (m_failed ? kFailedAttemptMs : kAttemptMs);
        if (nowMs - m_attemptStartMs < window) {
            return -1;
        }
        m_failed = false;
        const int count = usable.size();
        for (int step = 1; step <= count; ++step) {
            const int candidate = (m_active + step) % count;
            if (!usable.at(candidate)) {
                continue;
            }
            m_attemptStartMs = nowMs;
            if (candidate == m_active) {
                return -1;  // the only usable link: keep retrying it
            }
            m_active = candidate;
            return candidate;
        }
        m_attemptStartMs = nowMs;
        return -1;
    }

    // An outside decision (the user edited the device, or pressed Connect):
    // start over from `index` with a fresh attempt clock.
    void restart(int index, qint64 nowMs) {
        m_active = index;
        m_attemptStartMs = nowMs;
        m_failed = false;
    }

    // The link in use reported an error while not connected.
    void markFailed() {
        m_failed = true;
    }

    int active() const {
        return m_active;
    }

private:
    int m_active = 0;
    qint64 m_attemptStartMs = -1;
    bool m_failed = false;
};

// The BTP source_id a hub-channel device speaks as -- its own identity on the
// wire, not the robot's (that is Device::peerSourceId).
//
// Derived from the device's own id rather than generated, because the hub
// needs it to be STABLE. The hub cannot infer where a downstream frame should
// go: a BTP header has no destination field, and TERMINAL_IN carries no target
// in its payload either, so an operator binds child to robot by hand ("hub
// -bind <child> <peer>") and the hub keys that table on the child's source_id.
// A per-run random identity -- which is what this application uses for the
// console channel -- would silently invalidate every bind on every launch.
//
// FNV-1a over the id, forced non-zero because BTP reserves 0. A collision
// between two children would make the hub send one robot's traffic to the
// wrong device, so it is worth knowing the odds: with a 32-bit space and a
// handful of devices they are negligible, and two children of one hub with the
// same source_id would in any case be visible immediately as one of them never
// receiving anything.
inline quint32 hubChannelSourceId(const QString& deviceId) {
    if (deviceId.isEmpty()) {
        return 0;  // "not configured", same convention as an empty portName
    }
    quint32 hash = 2166136261u;
    const QByteArray utf8 = deviceId.toUtf8();
    for (char byte : utf8) {
        hash ^= quint8(byte);
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

// How far this device's link has actually come up -- more than the single
// connected/not bit the card used to paint (topico 35 D.2). The exact
// ambiguity this resolves: a serial device whose port opened but whose BTP
// handshake never completed ("connected and mute") looked identical to a
// working one.
enum class DeviceLinkState {
    Offline,        // no usable link: the transport is down (port closed / not
                    // configured / unplugged). For a hub child that means the
                    // cable to the DONGLE -- a robot that is merely quiet is
                    // PeerStale, not this, because the dongle is still relaying.
    TransportOnly,  // Serial/UsbHid: link open, but no BTP session yet
    Live,           // BTP session established (Serial/UsbHid), or a hub child
                    // whose robot's frames are actually reaching this TraceView
    PeerStale,      // hub child: the cable to the dongle is up, but the robot's
                    // presence is not confirmed -- either nothing has arrived
                    // from it yet (just connected / nothing subscribed) or its
                    // data has stopped
};

inline DeviceLinkState deviceLinkState(const Device& stored) {
    // Judged by the link actually in use, not the primary one -- a device
    // whose primary is TCP but is currently reached through a hub needs the
    // hub-child rules below.
    const Device device = effectiveDevice(stored);
    if (!device.connected) {
        return DeviceLinkState::Offline;
    }
    if (device.transportType == TransportType::HubChannel) {
        // No ENTER/HELLO on this transport, so `connected` only means the
        // dongle is relaying this child's frames -- it says nothing about the
        // robot. Whether the robot is actually there is
        // MainWindow::reconcileHubChildPresence's verdict, from the robot's own
        // end-to-end frames (primary) or the dongle's hub.peers view
        // (fallback). Green is earned; amber is the honest default.
        if (!device.peerPresenceKnown || !device.peerOnline) {
            return DeviceLinkState::PeerStale;
        }
        return DeviceLinkState::Live;
    }
    // btpVersion is set only by BtpHandshake::sessionEstablished and cleared
    // on disconnect (MainWindow::onDeviceConnectionStateChanged), so it is
    // exactly "a session is up right now".
    return device.btpVersion.isEmpty() ? DeviceLinkState::TransportOnly
                                       : DeviceLinkState::Live;
}

// The presence verdict MainWindow::reconcileHubChildPresence reaches each tick
// for one connected hub child, factored out as a pure function so the combine
// rule is testable without a MainWindow (tests/test_hubchildpresence.cpp).
// MainWindow keeps the parts that genuinely need it -- reading the two signals
// off a BtpBackend / the hub.peers accumulator, the boot-id, the toast, and
// pushing the result into the Device -- and calls this for the decision.
struct HubChildPresence {
    bool online = false;  // -> Device::peerOnline
    bool known = false;   // -> Device::peerPresenceKnown (sticky within a session)
    int offlineTicks = 0; // carried across ticks by the caller; opaque otherwise
};

// frameFresh   : a frame from the robot passed AEAD open within the freshness
//                window (BtpBackend::lastPeerDataFrameMsSinceEpoch) -- the
//                end-to-end, locally-verified signal.
// hubReadable  : the dongle's hub.peers view for this robot could be read this
//                tick (watch resolved and a sample decoded).
// hubOnline    : ...and that view said online. Ignored unless hubReadable.
// prev         : last tick's verdict for this child (its offlineTicks included).
// debounceTicks: consecutive all-signals-lost ticks tolerated before an
//                established green is given up (the fallback path's flap guard).
inline HubChildPresence hubChildPresence(bool frameFresh, bool hubReadable, bool hubOnline,
                                         const HubChildPresence& prev, int debounceTicks) {
    // Either positive signal means online; the dongle's opinion only counts
    // when its view was actually readable.
    const bool rawOnline = frameFresh || (hubReadable && hubOnline);

    HubChildPresence out;
    out.offlineTicks = rawOnline ? 0 : qMin(prev.offlineTicks + 1, debounceTicks);
    // The debounce only shields an already-established online from a brief gap;
    // it never manufactures one from nothing.
    out.online = rawOnline || (prev.online && out.offlineTicks < debounceTicks);
    // Sticky: once reconcile has had any verdict this session, "locating" does
    // not come back -- a later silence is the distinct "no data from robot".
    out.known = rawOnline || hubReadable || prev.known;
    return out;
}

// Mirrors dashboardItemToJson()'s shape/convention (dashboard/dashboarditem.h)
// -- one JSON object per Device, used by DevicesGrid::toJson()/fromJson() to
// persist the whole list into ProjectStore's "devices" section. `connected`,
// `btpVersion`, `btpId` and the `peer*` presence fields are deliberately not
// serialized: they're live-mirrored transport/session state (see each field's
// own comment above), not configuration -- a freshly loaded device always
// starts disconnected and unidentified until DeviceConnection actually opens
// its configured port and negotiates a fresh session.
QJsonObject deviceToJson(const Device& device);

// Returns a default-constructed Device and sets *ok = false if `object` is
// missing required fields.
Device deviceFromJson(const QJsonObject& object, bool* ok);

}  // namespace traceview
