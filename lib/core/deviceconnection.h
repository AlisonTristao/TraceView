#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

#include "devices/device.h"

class QTimer;

namespace traceview {

class Backend;
class SerialManager;
class UsbHidManager;
class Transport;

// Owns one device's real, independent connection: a Transport (raw bytes --
// concretely a SerialManager or a UsbHidManager, chosen once at construction
// by `transportType`) plus a Backend (protocol decode/encode) -- extracted
// from what MainWindow used to build exactly once for the whole app, now
// built once per Device instead (see the multi-device connection refactor
// plan). `commType` picks which concrete Backend gets constructed; only
// CommType::Btp exists today (devices/device.h), so this always builds a
// BtpBackend, but it's the switch point for a future protocol.
// `transportType` similarly picks the concrete Transport -- see
// TransportType (devices/device.h) -- and is threaded into BtpBackend too,
// since BTP's own wire framing differs per transport profile (Serial/COBS
// vs UsbHid, see BtpSession).
//
// Reconnection is ambient rather than a one-shot action: connectTo()
// records intent ("this device should be online at this target") separately
// from whatever the transport's isConnected() currently reports, and a
// timer keeps retrying open() while the two disagree -- covers both the
// first connection attempt for a freshly configured device and silently
// recovering from an unplug/replug. disconnectFrom() (or clearing the
// target via connectTo()) is the only thing that turns the intent back off;
// a transport error/drop does not.
class DeviceConnection : public QObject {
    Q_OBJECT

public:
    explicit DeviceConnection(CommType commType, TransportType transportType = TransportType::Serial,
                              QObject* parent = nullptr);

    // Non-null only when transportType == TransportType::Serial; nullptr
    // otherwise. Callers that need serial-only extras (writeCommand(),
    // lineTerminator()) must check for null first -- see
    // SerialWidgetBridge::wireWidget() for the pattern.
    SerialManager* serialManager() const { return m_serialManager; }
    // Non-null only when transportType == TransportType::UsbHid; nullptr
    // otherwise.
    UsbHidManager* usbHidManager() const { return m_usbHidManager; }
    Backend* backend() const { return m_backend; }

    TransportType transportType() const { return m_transportType; }

    bool isConnected() const;
    // Current intent: true from connectTo() (with a non-empty target) until
    // disconnectFrom() or a connectTo() with an empty target -- stays true
    // across a transport drop/retry, unlike isConnected(). What the
    // status-dot click toggle (DeviceCard::connectToggleRequested) reads to
    // decide whether to connect or disconnect.
    bool wantsConnection() const { return m_shouldBeConnected; }

    // Updates the connection target and marks intent "online" (unless
    // `target` is empty, which instead means "not configured" -- clears
    // intent and closes without ever attempting a connection). `target` is
    // a COM port name for TransportType::Serial or a hidapi device path for
    // TransportType::UsbHid (Device::portName/usbPath respectively --
    // MainWindow picks the right one before calling this); `baudRate` is
    // ignored for UsbHid. Safe to call again with different values while
    // already connected/retrying: a changed target or baud closes the
    // current connection (if any) and retries against the new target
    // immediately.
    void connectTo(const QString& target, qint32 baudRate);
    // Marks intent "offline" and closes. Stops the retry timer -- unlike a
    // transport drop, this does not come back on its own.
    void disconnectFrom();

    // No-op unless transportType == TransportType::Serial -- USB HID has no
    // console/raw-text channel for a line terminator to apply to (see
    // Device::lineTerminator's own comment).
    void setLineTerminator(int terminator);

signals:
    // Mirrors Transport::connectionStateChanged so callers don't have to
    // reach through serialManager()/usbHidManager() themselves.
    void connectionStateChanged(bool connected);
    // Mirrors Backend::deviceIdentified so callers don't have to reach
    // through backend() themselves.
    void deviceIdentified(const QString& btpVersion, const QString& btpId);

private:
    void attemptReconnect();

    TransportType m_transportType;
    Transport* m_transport = nullptr;
    SerialManager* m_serialManager = nullptr;
    UsbHidManager* m_usbHidManager = nullptr;
    Backend* m_backend = nullptr;
    QTimer* m_retryTimer;
    QString m_target;
    qint32 m_baudRate = 0;
    bool m_shouldBeConnected = false;
};

} // namespace traceview
