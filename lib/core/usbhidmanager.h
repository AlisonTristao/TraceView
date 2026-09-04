#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <atomic>

#include "transport.h"

class QThread;
struct hid_device_;
typedef struct hid_device_ hid_device;

namespace traceview {

// Owns one HID connection to the dongle's "usb_hid" BTP v1.1.0 transport
// profile (BTP/docs/fragmentation-and-transports.md section 3.3) via
// hidapi. Mirrors
// SerialManager's shape (see transport.h) so DeviceConnection can treat
// either transport the same way once open -- open() itself differs (a HID
// device path, not a COM port + baud rate) since the two have no common
// "target" shape.
//
// hidapi has no readyRead-equivalent (no selectable/waitable handle) --
// confirmed still true as of hidapi 0.15 (libusb/hidapi#202, open since
// 2020) -- so reading needs a dedicated thread polling hid_read_timeout()
// in a short loop, emitting dataReceived() via a queued connection back to
// whichever thread owns this object. write() is a one-shot call and safe to
// make directly from the caller's thread.
//
// Report framing (fragmentation-and-transports.md section 3.3): every report is
// [report_id][valid_length][up to 62 bytes of BTP frame, zero-padded]. A
// fixed-size HID report always transmits its full 63 data octets --
// USBHIDVendor pads a short write with zeros -- so without an explicit
// length prefix the receiver couldn't tell real data from padding.
// dataReceived()/write() both operate on the de-padded BTP frame bytes only
// -- this class adds/strips the report id and length prefix itself, so
// nothing above it (BtpSession, when built with btp::kUsbHidTransport) needs
// to know HID reports exist at all.
class UsbHidManager : public Transport {
    Q_OBJECT

public:
    // One enumerated HID device: `path` is what open() expects (hidapi's own
    // device path, platform-specific and not meant to be typed by a human);
    // `label` is what a picker combo should show instead.
    struct DeviceInfo {
        QString path;
        QString label;
    };

    // Every HID device currently visible to the OS, refreshed on every call
    // -- same "call again for a live list" contract as
    // SerialManager::availablePorts(). Deliberately not filtered by
    // vendor/product id: the dongle's HID descriptor doesn't yet reserve a
    // dedicated product id of its own (it still uses the arduino-esp32
    // default), so filtering here would either hide the real device or need
    // a value this class can't yet assume -- same "list everything, let the
    // user pick" precedent the port combo already sets for COM ports.
    static QVector<DeviceInfo> availableDevices();

    explicit UsbHidManager(QObject* parent = nullptr);
    ~UsbHidManager() override;

    // Closes any existing connection first, then opens the HID device at
    // `path`. Returns false and emits errorOccurred() on failure; emits
    // connectionStateChanged(true) on success.
    bool open(const QString& path);
    void close() override;

    bool isConnected() const override;
    QString path() const;

    // Writes one BTP frame (up to btp::kUsbHidMaxPayloadSize + header/CRC
    // octets -- btp::kUsbHidMaxFrameSize total). Returns false without
    // effect if not currently open, or if data is larger than one report can
    // carry -- callers (BtpSession in UsbHid mode) never produce an
    // oversized frame by construction, so this is a defensive check, not an
    // expected path.
    bool write(const QByteArray& data) override;

private:
    void readLoop();

    hid_device* m_handle = nullptr;
    QString m_path;
    QThread* m_thread = nullptr;
    // Set before the thread is asked to stop; readLoop() polls it between
    // hid_read_timeout() calls instead of blocking indefinitely, so close()
    // has a bounded wait instead of needing to interrupt a blocking read.
    std::atomic<bool> m_stopRequested{false};
    // Read from isConnected() (any thread) and written only by readLoop()
    // itself (true at the start, false right before it returns) -- m_handle
    // is touched by both open()/close() and the reader thread at different
    // times, so isConnected() reads this flag instead of racing on the
    // pointer.
    std::atomic<bool> m_connected{false};
};

}  // namespace traceview
