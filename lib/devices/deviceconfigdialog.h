#pragma once

#include <QDialog>
#include <QStringList>

#include "devices/device.h"
#include "telemetry/catalogtopicinfo.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QToolButton;

namespace traceview {

// Edits one Device: name/description directly, a "Connection" section
// (port/baud/line terminator -- the config DeviceConnection, core/
// deviceconnection.h, actually opens with, moved here now that each device
// owns its own connection instead of sharing the old single Run tab bar)
// plus a read-only status line, and a read-only "Reported by device" section
// (btpVersion/btpId, straight from the last HELLO_RESULT -- see
// protocol/btphandshake.h's sessionEstablished() and Device::btpVersion/
// btpId's own comments). Constructed with the Device to edit, read
// back via result() after exec() returns Accepted -- follows
// AboutDialog/DonateDialog's construction convention (see
// core/aboutdialog.h) -- Q_OBJECT so tr() resolves this class as its own
// translation context.
class DeviceConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceConfigDialog(const Device& initial, QWidget* parent = nullptr);

    // Valid once exec() == QDialog::Accepted: the initial Device with every
    // editable field replaced by the dialog's current contents. id/commType
    // are carried over unchanged -- this dialog never reassigns either --
    // and so are btpVersion/btpId: "Reported by device" is read-only here,
    // so whatever the initial Device already held (last HELLO_RESULT, or
    // empty) passes straight through.
    Device result() const;

    // Repopulates the Port combo from a fresh OS enumeration, preserving
    // whatever text is currently shown (typed manually, or the device's
    // already-configured port) even if it isn't in the new list -- e.g. a
    // device that's temporarily unplugged shouldn't lose its remembered
    // port name. Called once up front by whoever opens this dialog
    // (DevicesGrid, supplied by MainWindow) and again each time
    // refreshPortsRequested() fires.
    void setAvailablePorts(const QStringList& ports);

    // Repopulates the USB device combo from a fresh OS enumeration, same
    // "preserve the current selection even if it's not in the new list"
    // treatment as setAvailablePorts() above -- a device's own usbPath (its
    // hidapi device path, not the display label) is what's preserved,
    // synthesizing a placeholder entry if it isn't currently plugged in.
    // Called once up front and again each time refreshUsbDevicesRequested()
    // fires.
    void setAvailableUsbDevices(const QVector<UsbDeviceOption>& devices);

    // The devices this one could ride as a hub channel, as (id, display
    // name) pairs. The caller is responsible for leaving out this device
    // itself and anything that would form a cycle -- the dialog shows what
    // it is given. Same treatment as setAvailablePorts(): an
    // already-configured parent that is not in the list gets a placeholder
    // entry so opening the dialog cannot silently repoint the device.
    void setAvailableParentDevices(const QVector<QPair<QString, QString>>& parents);

    // Populates the read-only "Reported catalog" list below "Reported by
    // device" from this device's Backend::catalogTopics() (TelemetryCatalog,
    // via MANIFEST_DATA) -- one entry per (source, topic, schema_version)
    // the device has announced, each's field list in its tooltip. Unlike
    // setAvailablePorts(), there's no refresh button: whoever opens this
    // dialog (DevicesGrid) fetches the catalog once, up front, the same
    // moment it reads btpVersion/btpId off the Device it's editing.
    void setCatalogTopics(const QVector<CatalogTopicInfo>& topics);

signals:
    // Emitted when the user clicks the port list's refresh button.
    // DeviceConfigDialog can't query the OS port list itself --
    // traceview_devices doesn't depend on QSerialPort (see
    // lib/CMakeLists.txt) -- so the owner is expected to call
    // setAvailablePorts() again in response.
    void refreshPortsRequested();
    // Same reasoning as refreshPortsRequested() above, for the USB device
    // combo -- traceview_devices doesn't depend on hidapi either.
    void refreshUsbDevicesRequested();

private:
    void updateTransportFieldsVisibility();

    Device m_device;

    QLineEdit* m_nameEdit = nullptr;
    QPlainTextEdit* m_descriptionEdit = nullptr;

    QFormLayout* m_connectionLayout = nullptr;
    QComboBox* m_transportTypeCombo = nullptr;
    int m_portRowIndex = -1;
    int m_baudRowIndex = -1;
    int m_lineTerminatorRowIndex = -1;
    int m_usbDeviceRowIndex = -1;
    int m_parentRowIndex = -1;
    int m_peerSourceIdRowIndex = -1;
    int m_peerPasswordRowIndex = -1;
    int m_cachePasswordRowIndex = -1;

    QComboBox* m_portCombo = nullptr;
    QToolButton* m_refreshPortsButton = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QComboBox* m_lineTerminatorCombo = nullptr;
    QComboBox* m_usbDeviceCombo = nullptr;
    QToolButton* m_refreshUsbDevicesButton = nullptr;

    // Hub-channel rows: the device this one rides, the robot behind it, and
    // the endpoint-key password for that robot.
    QComboBox* m_parentCombo = nullptr;
    QLineEdit* m_peerSourceIdEdit = nullptr;
    QLineEdit* m_peerPasswordEdit = nullptr;
    QCheckBox* m_cachePasswordCheck = nullptr;

    QLabel* m_statusLabel = nullptr;

    // OTA group -- always visible regardless of transport type, since OTA is
    // a separate Wi-Fi/HTTP channel (see devices/device.h's otaAddress
    // comment) orthogonal to Serial/USB/Hub above. The OTA tab (lib/ota)
    // only displays this field; it's edited here, same convention as every
    // other static per-device setting.
    QLineEdit* m_otaAddressEdit = nullptr;

    // Read-only: populated from m_device.btpVersion/btpId (the last
    // HELLO_RESULT), never written back to the device in result().
    QLineEdit* m_btpVersionEdit = nullptr;
    QLineEdit* m_btpIdEdit = nullptr;

    // Read-only, populated by setCatalogTopics() -- see that method's own
    // comment.
    QListWidget* m_catalogList = nullptr;
};

} // namespace traceview
