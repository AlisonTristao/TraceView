#pragma once

#include <QByteArray>
#include <QJniObject>
#include <QString>
#include <QVector>

#include "devices/device.h"
#include "serialtransport.h"

namespace traceview {

// TransportType::Serial on Android. Qt has no QSerialPort backend there and an
// app cannot open /dev/ttyACM* without root, so this drives the device through
// the Android USB Host API instead -- android/src/io/github/alisontristao/
// traceview/UsbSerialBridge.java, a minimal CDC-ACM driver, reached over JNI.
// Only CDC-ACM devices are supported (the dongle and ESP32 boards with native
// USB); bridge chips (CH340/CP210x/FTDI) are not.
//
// Port names are stable keys, "usb:VVVV:PPPP[:serial]" (hex VID/PID, plus the
// USB serial number when Android lets the app read it -- it needs the device
// permission first on API 29+). The OS's own device path
// (/dev/bus/usb/001/007) changes on every replug and is never stored. A key
// without a serial, or one whose device does not expose its serial yet,
// matches by VID:PID.
//
// Everything USB happens on Java threads (an I/O executor and a reader
// thread); their callbacks come back through static JNI natives and are
// queued onto this object's thread before any signal is emitted, so the
// Transport contract is the same as SerialManager's. Unlike SerialManager,
// open() is asynchronous whenever Android has to ask the user for the USB
// permission first -- the outcome arrives later, see SerialTransport::open().
class AndroidUsbSerialTransport : public SerialTransport {
    Q_OBJECT

public:
    explicit AndroidUsbSerialTransport(QObject* parent = nullptr);
    ~AndroidUsbSerialTransport() override;

    // CDC-ACM devices currently attached, as (key, product label) pairs.
    // Refreshed on every call.
    static QVector<SerialPortOption> availablePorts();

    bool open(const QString& portName, qint32 baudRate) override;
    void close() override;

    bool isConnected() const override;
    QString portName() const override;
    bool isDonglePort(const QString& portName) const override;
    qint32 baudRate() const override;

    // Queues `data` on the Java I/O thread; true means queued, not yet on the
    // wire (same meaning TcpTransport's write() has).
    bool write(const QByteArray& data) override;
    // Waits until every write queued so far has been handed to the USB stack.
    bool drainWrites(int timeoutMs) override;

signals:
    // Any USB device was plugged in. DeviceConnection retries on this right
    // away instead of waiting for its reconnect timer.
    void deviceAttached();

private:
    friend struct AndroidUsbSerialNatives;

    void handleOpened(int attempt);
    void handleData(int attempt, const QByteArray& data);
    void handleError(int attempt, const QString& message, bool permissionDenied);
    void handleLost(int attempt, const QString& message);
    void handleDeviceAttached();

    QJniObject m_bridge;
    qint64 m_handle = 0;
    // Bumped by every open(); callbacks carrying an older value belong to an
    // attempt that close() or a newer open() already superseded.
    int m_attempt = 0;
    bool m_connected = false;
    bool m_opening = false;
    QString m_portName;
    qint32 m_baudRate = 0;
    // Key the user refused the USB permission for. Not asked again until the
    // device is replugged (deviceAttached) or the app restarts -- otherwise
    // every reconnect tick would throw the system dialog back up.
    QString m_permissionDeniedFor;
};

}  // namespace traceview
