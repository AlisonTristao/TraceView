#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace traceview {

// Common shape SerialManager and UsbHidManager both implement: bytes in,
// bytes out, connection-state/error signals -- what DeviceConnection wires
// generically to a device's Backend (feedBytes/bytesToWrite), regardless of
// which concrete transport backs a given device. Introduced alongside
// UsbHidManager (the second real transport, see CONTRIBUTING.md's "no
// premature abstraction" -- this is the second case, not a speculative one).
//
// open() is deliberately NOT part of this interface: SerialManager's target
// is a COM port name plus a baud rate, UsbHidManager's is a HID device path
// with no baud concept at all -- different enough that forcing one signature
// here would just be a stringly-typed muddle. DeviceConnection knows which
// concrete type it built (see Device::transportType) and calls the specific
// open() directly; this interface only covers what every transport shares
// once a connection exists.
class Transport : public QObject {
    Q_OBJECT

public:
    explicit Transport(QObject* parent = nullptr) : QObject(parent) {}

    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    // Writes raw bytes to the transport. Returns false without effect if not
    // currently open -- callers that can't guarantee an open connection
    // should treat a false return as "went nowhere," not an error to surface
    // (same contract SerialManager::write() already had before this
    // interface existed).
    virtual bool write(const QByteArray& data) = 0;

signals:
    void connectionStateChanged(bool connected);
    // Raw bytes as they arrive off the wire -- no frame decoding, no
    // protocol awareness. What that means concretely differs per transport
    // (a byte range off a COM port stream vs. one HID report's worth of
    // already-de-padded payload), but both hand fully assembled logical
    // chunks to the same Backend::feedBytes() slot either way.
    void dataReceived(const QByteArray& data);
    // Transport-level errors only (open failure, device unplugged
    // mid-session, etc.) -- never for malformed protocol data, which isn't
    // this layer's concern.
    void errorOccurred(const QString& message);
};

} // namespace traceview
