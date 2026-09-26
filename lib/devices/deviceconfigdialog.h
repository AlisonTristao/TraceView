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
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace traceview {

class DeviceLinksTable;
class RobotLinksPanel;

// Edits one Device: a "Connection" section
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
    // Each option's `label` is shown; its `name` is what result() stores in
    // Device::portName (identical on desktop, see SerialPortOption).
    void setAvailablePorts(const QVector<SerialPortOption>& ports);

    // Adds (or updates) one scan result in the BLE address combo -- called
    // repeatedly, live, as BleDiscoveryService reports peripherals while a
    // scan is running (see scanBleRequested()). De-duplicated by address:
    // a peripheral seen again (most backends re-emit per advertisement)
    // refreshes its displayed name rather than appending a second entry.
    // Never touches the currently-typed/selected text, same "don't clobber
    // what the user is looking at" treatment as setAvailablePorts().
    void addDiscoveredBleDevice(const QString& name, const QString& address);

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

    // Populates the read-only "Reported catalog" panel (right column) from
    // this device's Backend::catalogTopics() (TelemetryCatalog, via
    // MANIFEST_DATA) -- one indented block per (source, topic,
    // schema_version) the device has announced, each with its field list
    // nested underneath. Unlike setAvailablePorts(), there's no refresh
    // button: whoever opens this dialog (DevicesGrid) fetches the catalog
    // once, up front, the same moment it reads btpVersion/btpId off the
    // Device it's editing.
    void setCatalogTopics(const QVector<CatalogTopicInfo>& topics);

    // Refreshes the read-only "Reported by device" fields in place, without
    // closing the dialog -- called (via DevicesGrid) each time a fresh
    // HELLO_RESULT arrives after the user clicks Connect below. Also updates
    // m_device itself, not just the line edits, so a later OK doesn't
    // clobber this back to whatever btpVersion/btpId held when the dialog
    // was first opened (see result()'s own comment).
    void setReportedIdentity(const QString& btpVersion, const QString& btpId);

    // Refreshes the read-only "Reported by device" info list (the device's
    // MANIFEST_DATA source_info block, BTP's docs/commands.md section 3.12) in
    // place. Same treatment as setReportedIdentity() -- updates m_device too,
    // arrives asynchronously after the handshake, called with an empty vector
    // when the connection drops.
    void setReportedInfo(const QVector<DeviceInfoRecord>& info);

    // What this device described on its last connection (the manifest cache,
    // see ManifestStore): shown in "Reported by device"/"Reported catalog",
    // marked as saved, wherever nothing live has arrived -- so the dialog of
    // a disconnected device is not blank. Live data replaces it as it comes
    // in and gives way back to it if it goes away. Never copied into
    // m_device: result() carries live session state only.
    void setCachedDescription(const QVector<CatalogTopicInfo>& topics,
                              const QVector<DeviceInfoRecord>& info);

    // Refreshes the read-only status line the same way, and for the same
    // reason -- see setReportedIdentity() above.
    void setConnectionStatus(bool connected);

    // Repopulates the "Robot source_id" combo with whatever peers the hub
    // selected in "Through device" has actually reported, live -- see
    // MainWindow::hubPeersFor(). Preserves whatever text is currently
    // typed/selected the same way populateTopicCombo() does for a chart's
    // Topic field: a robot that hasn't been heard yet (or never will be, if
    // this project is being edited offline) must stay a valid, keepable
    // value. Called once up front and again on a timer for as long as the
    // dialog stays open (see DevicesGrid::handleConfigRequested()) -- unlike
    // the port/USB lists above, peers arrive continuously over telemetry
    // rather than only on a Refresh click.
    void setAvailableHubPeers(const QVector<HubPeer>& peers);

    // Device::id of whichever entry is currently selected in "Through
    // device" -- what the caller polls setAvailableHubPeers() for. Empty
    // when nothing is selected yet.
    QString currentParentDeviceId() const;

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
    // Emitted when the user clicks the BLE "Scan"/"Stop" toggle button --
    // true to start, false to stop. traceview_devices can't scan itself
    // (doesn't depend on Qt6::Bluetooth -- see lib/CMakeLists.txt); the
    // owner is expected to drive a BleDiscoveryService and call
    // addDiscoveredBleDevice() as results arrive.
    void scanBleRequested(bool start);
    // Emitted when the user clicks Connect -- unlike OK/Cancel this does not
    // close the dialog. The owner (DevicesGrid) is expected to read back
    // result() and apply it (same as an OK) so the edited port/baud/etc.
    // actually take effect and a (re)connect attempt starts, while this
    // dialog stays open to show what comes back from it.
    void applyRequested();

private:
    // Shows the rows under the connections table that only some link types
    // need: the password (TCP/BLE/Hub), the terminator (Serial), this
    // device's id (Hub).
    void updateConnectionRows();

    Device m_device;

    QLineEdit* m_nameEdit = nullptr;

    // Every way to reach the device -- row 1 is the primary link, the rest
    // Device::extraLinks. See DeviceLinksTable.
    DeviceLinksTable* m_linksTable = nullptr;
    // The simple view of the same links: one robot name, one checkbox per
    // kind of link. See RobotLinksPanel.
    RobotLinksPanel* m_linksPanel = nullptr;
    // Shows/hides m_linksTable.
    QToolButton* m_advancedButton = nullptr;
    // The device-wide rows under the table.
    QFormLayout* m_connectionLayout = nullptr;
    int m_lineTerminatorRowIndex = -1;
    int m_childSourceIdRowIndex = -1;
    int m_peerPasswordRowIndex = -1;
    int m_cachePasswordRowIndex = -1;
    QComboBox* m_lineTerminatorCombo = nullptr;
    // Read-only: this device's own source_id on the wire (hubChannelSourceId()
    // in devices/device.h), the value an operator must pass as the first
    // argument to the dongle's `hub -bind` -- shown here because nothing else
    // in the UI ever surfaces it (see docs/DEVICES.md "Hub channels").
    QLabel* m_childSourceIdLabel = nullptr;
    QLineEdit* m_peerPasswordEdit = nullptr;
    QCheckBox* m_cachePasswordCheck = nullptr;

    QLabel* m_statusLabel = nullptr;

    // OTA group -- always visible regardless of transport type, since OTA is
    // a separate Wi-Fi/HTTP channel (see devices/device.h's otaAddress
    // comment) orthogonal to Serial/USB/Hub above. The OTA tab (lib/ota)
    // only displays this field; it's edited here, same convention as every
    // other static per-device setting.
    QLineEdit* m_otaAddressEdit = nullptr;
    // "Use reported" button next to the address field. The device announces
    // its own OTA hostname in the MANIFEST_DATA source_info block (key
    // "ota_endpoint", BTP's docs/commands.md section 3.12) -- the device
    // knows it better than a hand-typed guess. Shown only while a reported
    // endpoint exists and differs from what's typed; clicking it fills the
    // field. Never overwrites silently -- otaAddress is persisted config, the
    // reported value is live session state.
    QToolButton* m_useReportedOtaButton = nullptr;
    QString m_reportedOtaEndpoint;
    void updateReportedOtaHint();
    // Redraw the catalog list / info label from live data, else the cache.
    void renderCatalog();
    void renderReportedInfo();
    // The OTA tab (lib/ota/otatab.h) can also edit otaPassword/
    // cacheOtaPassword -- from its own per-row password field, committed via
    // OtaTab::passwordCacheChanged() -- but a device is configured here
    // first, before it ever has a row in that tab to type into, so this
    // dialog offers the same field too. Same opt-in-only "save in project
    // file" convention as the Hub group's Robot password below.
    QLineEdit* m_otaPasswordEdit = nullptr;
    QCheckBox* m_cacheOtaPasswordCheck = nullptr;

    // Read-only: populated from m_device.btpVersion/btpId (the last
    // HELLO_RESULT), never written back to the device in result().
    QLineEdit* m_btpVersionEdit = nullptr;
    // Read-only multi-line "label: value" view of m_device.reportedInfo (the
    // MANIFEST_DATA source_info block), populated by setReportedInfo(). Shows
    // a placeholder while the device has reported nothing, same as
    // m_btpVersionEdit.
    QLabel* m_reportedInfoLabel = nullptr;
    QLineEdit* m_btpIdEdit = nullptr;

    // Read-only, populated by setCatalogTopics() -- see that method's own
    // comment. Plain text (not a QListWidget) so the catalog can be shown as
    // indented, JSON-like text rather than one truncated line per topic.
    QPlainTextEdit* m_catalogList = nullptr;

    // Last setCatalogTopics(), and setCachedDescription()'s fallback.
    QVector<CatalogTopicInfo> m_liveTopics;
    QVector<CatalogTopicInfo> m_cachedTopics;
    QVector<DeviceInfoRecord> m_cachedInfo;
};

}  // namespace traceview
