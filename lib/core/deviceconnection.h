#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

#include "devices/device.h"

class QTimer;

namespace traceview {

class Backend;
class SerialManager;

// Owns one device's real, independent serial connection: a SerialManager
// (raw bytes) plus a Backend (protocol decode/encode) -- extracted from what
// MainWindow used to build exactly once for the whole app, now built once
// per Device instead (see the multi-device connection refactor plan).
// `commType` picks which concrete Backend gets constructed; only
// CommType::Btp exists today (devices/device.h), so this always builds a
// BtpBackend, but it's the switch point for a future transport.
//
// Reconnection is ambient rather than a one-shot action: connectTo()
// records intent ("this device should be online at this port/baud")
// separately from whatever SerialManager::isConnected() currently reports,
// and a timer keeps retrying open() while the two disagree -- covers both
// the first connection attempt for a freshly configured device and silently
// recovering from an unplug/replug. disconnectFrom() (or clearing the
// portName via connectTo()) is the only thing that turns the intent back
// off; a transport error/drop does not.
class DeviceConnection : public QObject {
    Q_OBJECT

public:
    explicit DeviceConnection(CommType commType, QObject* parent = nullptr);

    SerialManager* serialManager() const { return m_serialManager; }
    Backend* backend() const { return m_backend; }

    bool isConnected() const;
    // Current intent: true from connectTo() (with a non-empty portName)
    // until disconnectFrom() or a connectTo() with an empty portName --
    // stays true across a transport drop/retry, unlike isConnected(). What
    // the status-dot click toggle (DeviceCard::connectToggleRequested) reads
    // to decide whether to connect or disconnect.
    bool wantsConnection() const { return m_shouldBeConnected; }

    // Updates the connection target and marks intent "online" (unless
    // `portName` is empty, which instead means "not configured" -- clears
    // intent and closes without ever attempting a connection). Safe to call
    // again with different values while already connected/retrying: a
    // changed port or baud closes the current connection (if any) and
    // retries against the new target immediately.
    void connectTo(const QString& portName, qint32 baudRate);
    // Marks intent "offline" and closes. Stops the retry timer -- unlike a
    // transport drop, this does not come back on its own.
    void disconnectFrom();

    void setLineTerminator(int terminator);

signals:
    // Mirrors SerialManager::connectionStateChanged so callers don't have to
    // reach through serialManager() themselves.
    void connectionStateChanged(bool connected);
    // Mirrors Backend::deviceIdentified so callers don't have to reach
    // through backend() themselves.
    void deviceIdentified(const QString& btpVersion, const QString& btpId);

private:
    void attemptReconnect();

    SerialManager* m_serialManager;
    Backend* m_backend = nullptr;
    QTimer* m_retryTimer;
    QString m_portName;
    qint32 m_baudRate = 0;
    bool m_shouldBeConnected = false;
};

} // namespace traceview
