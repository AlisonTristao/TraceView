#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtGlobal>

#include "core/transport.h"

namespace traceview {

class BtpBackend;
class DeviceConnection;

// The third Transport (after SerialManager and UsbHidManager): one that has no
// port of its own and instead multiplexes over ANOTHER device's connection.
//
// This is what turns the dongle from a cable into a hub. The desktop opens one
// serial connection to the dongle and talks to it as an ordinary BTP device;
// every robot behind the dongle's radio then becomes its own Device, with its
// own manifest, charts and terminal, riding that same single cable. One cable,
// several devices, each talking end to end with its own robot.
//
// Why this fits without a refactor: Transport is already the seam every
// DeviceConnection is wired against (close/isConnected/write plus signals), so
// a transport whose "wire" happens to be a parent Device is a third
// implementation and nothing above it has to learn a new shape.
//
// ---------------------------------------------------------------------------
// How a frame gets in and out
// ---------------------------------------------------------------------------
// Inbound: the parent's BtpSession decodes a frame off the cable and offers
// its raw octets, tagged with the source_id from its header, to every attached
// child (BtpBackend::hubFrameBytesReceived). This claims the ones whose
// source_id is its own robot's and ignores the rest. That is the entire demux
// rule -- there is no routing table, and no per-message-type special case:
// telemetry, log, terminal, manifest and command result all sort the same way,
// because identity is what a channel is defined by.
//
// Outbound: the child's own BtpSession encodes under the ESP-NOW profile (the
// frame has to fit in a radio datagram, since that is where it is going) and
// hands the finished octets here; this passes them to the parent, which adds
// only the cable's framing. The dongle unwraps that framing and writes the
// same octets on the air.
//
// Nothing in either direction re-encodes, re-fragments or recomputes a CRC.
// The octets a child produces reach the robot untouched, which is what lets an
// end-to-end seal verify at the far end -- the parent holds no key for the
// traffic it carries.
class HubTransport : public Transport {
    Q_OBJECT

public:
    // `peerSourceId` is the ADDRESS of the robot behind the hub, and it is the
    // real one: a BTP source_id, stable across reboots of anything. It is
    // deliberately not the "channel" index the dongle publishes in hub.peers,
    // which is only a display label assigned in the order peers were first
    // heard and is not stable across a dongle reboot. Persisting the index
    // instead of this would silently repoint a saved project at a different
    // robot after a reboot -- see Device::peerSourceId.
    //
    // Zero means "not configured": isConnected() then stays false and nothing
    // is ever claimed, matching how an empty portName means the same for a
    // serial device.
    explicit HubTransport(quint32 peerSourceId, QObject* parent = nullptr);

    // Binds this child to the device that carries it. Safe to call again to
    // move to a different parent, and with nullptr to detach; the previous
    // parent's signals are disconnected either way.
    //
    // Takes the DeviceConnection rather than the BtpBackend because the parent
    // being *connected* is half of this transport's own connected state, and
    // that lives on DeviceConnection.
    void attachTo(DeviceConnection* parent);

    quint32 peerSourceId() const {
        return m_peerSourceId;
    }

    // Repoints this child at a different robot. Separate from the constructor
    // because a Device can be reconfigured while it exists, exactly as a
    // serial device can be moved to another port; zero puts it back to "not
    // configured".
    void setPeerSourceId(quint32 peerSourceId);

    void close() override;

    // True only when the parent is connected AND this child has a configured
    // peer. There is no third state to report: a child cannot be reachable
    // through a parent that is not.
    //
    // Whether the robot itself is currently answering is a separate question,
    // answered by the hub's own hub.peers topic, and deliberately not folded
    // in here -- a transport reports whether its link exists, not whether the
    // far end is healthy.
    bool isConnected() const override;

    // Hands one already-encoded frame to the parent to put on the wire.
    // Returns false without effect when there is no parent, the parent is not
    // connected, or the parent refuses the frame -- the same "went nowhere"
    // contract every other Transport::write() has.
    bool write(const QByteArray& frame) override;

private:
    void onParentFrameBytes(quint32 sourceId, const QByteArray& raw);
    void onParentConnectionChanged(bool connected);
    void detach();

    quint32 m_peerSourceId;
    // QPointer, not a raw pointer: a parent Device can be deleted while a
    // child still holds a reference to it, and a dangling parent here would
    // turn a UI action into a crash rather than a disconnect.
    QPointer<DeviceConnection> m_parent;
    QPointer<BtpBackend> m_parentBackend;
    // Mirrors the last state seen from the parent so close() can report
    // disconnected without waiting for the parent to change.
    bool m_open = false;
};

}  // namespace traceview
