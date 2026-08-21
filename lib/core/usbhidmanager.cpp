#include "usbhidmanager.h"

#include <QThread>

#include <hidapi.h>

#include <cstring>

namespace traceview {

namespace {

// "usb_hid" transport profile (BTP/docs/TRANSPORT_USB_HID.md section 2):
// one physical HID report is 64 octets total -- 1 Report ID octet plus 63
// data octets declared by the firmware's USBHIDVendor descriptor. Report ID
// matches arduino-esp32's fixed HID_REPORT_ID_VENDOR (USBHID.h enum), the
// vendor interface's only report id.
constexpr unsigned char kReportId = 6;
constexpr int kReportDataSize = 63;              // matches USBHIDVendor(63, ...)
constexpr int kReportBufferSize = 1 + kReportDataSize;  // report id + data
constexpr int kReadTimeoutMs = 100;              // short poll, see class comment

} // namespace

QVector<UsbHidManager::DeviceInfo> UsbHidManager::availableDevices() {
    QVector<DeviceInfo> devices;
    if (hid_init() != 0) {
        return devices;
    }
    hid_device_info* root = hid_enumerate(0x0, 0x0);
    for (hid_device_info* info = root; info != nullptr; info = info->next) {
        DeviceInfo device;
        device.path = QString::fromLocal8Bit(info->path);

        const QString manufacturer =
            info->manufacturer_string ? QString::fromWCharArray(info->manufacturer_string) : QString();
        const QString product = info->product_string ? QString::fromWCharArray(info->product_string) : QString();
        QString label = QString("%1:%2")
                             .arg(QString::number(info->vendor_id, 16).rightJustified(4, '0'))
                             .arg(QString::number(info->product_id, 16).rightJustified(4, '0'));
        const QString name = QString("%1 %2").arg(manufacturer, product).trimmed();
        if (!name.isEmpty()) {
            label += QString::fromUtf8(" \xE2\x80\x94 ") + name;  // " — "
        }
        device.label = label;
        devices.append(device);
    }
    hid_free_enumeration(root);
    return devices;
}

UsbHidManager::UsbHidManager(QObject* parent) : Transport(parent) {}

UsbHidManager::~UsbHidManager() {
    close();
}

bool UsbHidManager::open(const QString& path) {
    close();

    if (path.isEmpty() || hid_init() != 0) {
        return false;
    }

    const QByteArray pathBytes = path.toLocal8Bit();
    hid_device* handle = hid_open_path(pathBytes.constData());
    if (!handle) {
        emit errorOccurred(tr("Could not open HID device: %1").arg(path));
        return false;
    }

    m_handle = handle;
    m_path = path;
    m_stopRequested.store(false);

    m_thread = QThread::create([this]() { readLoop(); });
    m_connected.store(true);
    m_thread->start();

    emit connectionStateChanged(true);
    return true;
}

void UsbHidManager::close() {
    if (!m_thread) {
        return;  // never opened, or already fully closed
    }
    m_stopRequested.store(true);
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    m_path.clear();
    // connectionStateChanged(false) is emitted by readLoop() itself, right
    // before it returns -- both the requested-stop path (here) and the
    // device-error exit path (unplugged mid-session) funnel through the
    // same single exit point, so emitting it again here would double-report
    // the same drop.
}

bool UsbHidManager::isConnected() const {
    return m_connected.load();
}

QString UsbHidManager::path() const {
    return m_path;
}

bool UsbHidManager::write(const QByteArray& data) {
    if (!isConnected()) {
        return false;
    }
    // kReportDataSize - 1: one of the 63 data octets is this session's own
    // valid-length prefix (see the class comment) -- BtpSession built with
    // btp::TransportProfile::UsbHid never produces a frame larger than
    // btp::kUsbHidMaxFrameSize (62) by construction, so this is a defensive
    // check, not an expected path.
    if (data.size() > kReportDataSize - 1) {
        return false;
    }

    unsigned char buffer[kReportBufferSize] = {0};
    buffer[0] = kReportId;
    buffer[1] = static_cast<unsigned char>(data.size());
    if (!data.isEmpty()) {
        std::memcpy(buffer + 2, data.constData(), static_cast<std::size_t>(data.size()));
    }

    const int written = hid_write(m_handle, buffer, sizeof(buffer));
    return written == static_cast<int>(sizeof(buffer));
}

void UsbHidManager::readLoop() {
    unsigned char buffer[kReportBufferSize];
    while (!m_stopRequested.load()) {
        const int result = hid_read_timeout(m_handle, buffer, sizeof(buffer), kReadTimeoutMs);
        if (result < 0) {
            emit errorOccurred(tr("HID device disconnected"));
            break;
        }
        if (result < 2) {
            // 0 = timeout, nothing to do; 1 = a malformed report shorter
            // than [report_id][valid_length] -- either way, loop again.
            continue;
        }
        // buffer[0] is the report id (hidapi includes it on read for a
        // numbered-report device, which this one is); buffer[1] is this
        // session's own valid-length prefix, the rest is zero padding.
        const int validLength = std::min<int>(buffer[1], kReportDataSize - 1);
        if (validLength > 0) {
            emit dataReceived(QByteArray(reinterpret_cast<const char*>(buffer + 2), validLength));
        }
    }

    // hid_close() from a different thread than the one blocked inside
    // hid_read_timeout() is not fully safe on the Windows backend (CancelIo
    // vs CancelIoEx) -- closing here, from the same thread that was doing
    // the reading, and only after the loop above has already returned,
    // avoids that race entirely (see UsbHidManager class comment / session
    // notes on hidapi pitfalls).
    m_connected.store(false);
    hid_close(m_handle);
    m_handle = nullptr;
    emit connectionStateChanged(false);
}

} // namespace traceview
