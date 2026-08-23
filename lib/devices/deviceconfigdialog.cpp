#include "deviceconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace traceview {

namespace {

// One line for m_catalogList -- e.g. "motor_state — source 0x11223344,
// topic 0x0101 (v1, PACKED_LE)". The field list (name/type/unit) goes in the
// item's tooltip instead of the line itself, so the list stays scannable
// even for a topic with many fields.
QString catalogTopicLine(const CatalogTopicInfo& topic) {
    const QString label = topic.name.isEmpty() ? QObject::tr("(unnamed topic)") : topic.name;
    return QString("%1 \xE2\x80\x94 source 0x%2, topic 0x%3 (v%4, %5)")
        .arg(label)
        .arg(topic.sourceId, 8, 16, QChar('0'))
        .arg(topic.topicId, 4, 16, QChar('0'))
        .arg(topic.schemaVersion)
        .arg(topic.encoding);
}

QString catalogTopicTooltip(const CatalogTopicInfo& topic) {
    if (topic.fields.isEmpty()) {
        return QObject::tr("No fields declared.");
    }
    QStringList lines;
    lines.reserve(topic.fields.size());
    for (const CatalogTopicField& field : topic.fields) {
        QString line = QString("%1: %2").arg(field.name.isEmpty() ? QObject::tr("(unnamed)") : field.name, field.type);
        if (!field.unit.isEmpty() && field.unit != QStringLiteral("1")) {
            line += QString(" (%1)").arg(field.unit);
        }
        lines.append(line);
    }
    return lines.join('\n');
}

}  // namespace

DeviceConfigDialog::DeviceConfigDialog(const Device& initial, QWidget* parent)
    : QDialog(parent), m_device(initial) {
    setWindowTitle(tr("Device Settings"));
    setMinimumWidth(360);

    m_nameEdit = new QLineEdit(m_device.name, this);
    m_descriptionEdit = new QPlainTextEdit(m_device.description, this);
    m_descriptionEdit->setFixedHeight(64);

    auto* formLayout = new QFormLayout;
    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Description:"), m_descriptionEdit);

    // Connection group -- transport type plus whichever of
    // port/baud/line-terminator (Serial) or USB device (UsbHid) it needs,
    // the config a DeviceConnection (core/deviceconnection.h) actually opens
    // with. Lives here now, one per device, instead of the old single
    // global Run tab bar.
    auto* connectionGroup = new QGroupBox(tr("Connection"), this);
    m_connectionLayout = new QFormLayout(connectionGroup);

    m_transportTypeCombo = new QComboBox(connectionGroup);
    m_transportTypeCombo->addItem(transportTypeLabel(TransportType::Serial), int(TransportType::Serial));
    m_transportTypeCombo->addItem(transportTypeLabel(TransportType::UsbHid), int(TransportType::UsbHid));
    m_transportTypeCombo->addItem(transportTypeLabel(TransportType::HubChannel), int(TransportType::HubChannel));
    const int transportTypeIndex = m_transportTypeCombo->findData(int(m_device.transportType));
    m_transportTypeCombo->setCurrentIndex(transportTypeIndex >= 0 ? transportTypeIndex : 0);
    connect(m_transportTypeCombo, &QComboBox::currentIndexChanged, this,
            &DeviceConfigDialog::updateTransportFieldsVisibility);
    m_connectionLayout->addRow(tr("Transport:"), m_transportTypeCombo);

    m_portCombo = new QComboBox(connectionGroup);
    m_portCombo->setEditable(true);
    m_portCombo->setToolTip(tr("Serial port"));
    if (!m_device.portName.isEmpty()) {
        m_portCombo->addItem(m_device.portName);
    }
    m_portCombo->setCurrentText(m_device.portName);

    m_refreshPortsButton = new QToolButton(connectionGroup);
    m_refreshPortsButton->setText(QString::fromUtf8("\xE2\x9F\xB3")); // ⟳
    m_refreshPortsButton->setToolTip(tr("Refresh port list"));
    m_refreshPortsButton->setAutoRaise(true);
    connect(m_refreshPortsButton, &QToolButton::clicked, this, &DeviceConfigDialog::refreshPortsRequested);

    auto* portRow = new QHBoxLayout;
    portRow->addWidget(m_portCombo, /*stretch=*/1);
    portRow->addWidget(m_refreshPortsButton);

    m_portRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Port:"), portRow);

    // Same list/default the old Run tab offered (extended for BTP v1 dongle
    // rates, see docs/PROTOCOL.md), now per-device rather than global.
    m_baudCombo = new QComboBox(connectionGroup);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "1000000",
                            "2000000", "3000000", "5000000"});
    m_baudCombo->setCurrentText(QString::number(m_device.baudRate));
    m_baudCombo->setToolTip(tr("Baud rate (type a custom value if yours isn't listed)"));
    m_baudCombo->setValidator(new QIntValidator(1, 10000000, m_baudCombo));
    m_baudRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Baud rate:"), m_baudCombo);

    // Values match traceview::LineTerminator's ordinals (core/serialmanager.h)
    // -- Device::lineTerminator stores that same ordinal as a plain int since
    // this module can't depend on that enum's header (see device.h).
    m_lineTerminatorCombo = new QComboBox(connectionGroup);
    m_lineTerminatorCombo->addItem(tr("None"), 0);
    m_lineTerminatorCombo->addItem(tr("LF (\\n)"), 1);
    m_lineTerminatorCombo->addItem(tr("CR (\\r)"), 2);
    m_lineTerminatorCombo->addItem(tr("CRLF (\\r\\n)"), 3);
    const int terminatorIndex = m_lineTerminatorCombo->findData(m_device.lineTerminator);
    m_lineTerminatorCombo->setCurrentIndex(terminatorIndex >= 0 ? terminatorIndex : 1);
    m_lineTerminatorCombo->setToolTip(tr("Line terminator appended to control-widget commands sent to this device. "
                                          "Doesn't affect its serial terminal's raw keystrokes."));
    m_lineTerminatorRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Line terminator:"), m_lineTerminatorCombo);

    // USB device picker -- shown instead of the three rows above when
    // Transport is set to USB (see updateTransportFieldsVisibility()).
    // hidapi device paths aren't something a user would ever type by hand
    // (unlike a COM port name), so unlike m_portCombo this one isn't
    // editable: setAvailableUsbDevices() synthesizes a placeholder entry for
    // an already-configured-but-not-currently-plugged-in device instead.
    m_usbDeviceCombo = new QComboBox(connectionGroup);
    m_usbDeviceCombo->setToolTip(tr("USB HID device"));
    if (!m_device.usbPath.isEmpty()) {
        m_usbDeviceCombo->addItem(m_device.usbPath, m_device.usbPath);
    }

    m_refreshUsbDevicesButton = new QToolButton(connectionGroup);
    m_refreshUsbDevicesButton->setText(QString::fromUtf8("\xE2\x9F\xB3")); // ⟳
    m_refreshUsbDevicesButton->setToolTip(tr("Refresh USB device list"));
    m_refreshUsbDevicesButton->setAutoRaise(true);
    connect(m_refreshUsbDevicesButton, &QToolButton::clicked, this, &DeviceConfigDialog::refreshUsbDevicesRequested);

    auto* usbDeviceRow = new QHBoxLayout;
    usbDeviceRow->addWidget(m_usbDeviceCombo, /*stretch=*/1);
    usbDeviceRow->addWidget(m_refreshUsbDevicesButton);
    m_usbDeviceRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("USB device:"), usbDeviceRow);

    // Hub-channel rows -- shown instead of every row above when Transport is
    // set to Hub. A hub channel has no port and no baud rate: its "wire" is
    // another device's connection.
    m_parentCombo = new QComboBox(connectionGroup);
    m_parentCombo->setToolTip(tr("The device whose connection carries this one."));
    if (!m_device.parentDeviceId.isEmpty()) {
        m_parentCombo->addItem(m_device.parentDeviceId, m_device.parentDeviceId);
    }
    m_parentRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Through device:"), m_parentCombo);

    // Typed and shown as hex because that is how a source_id appears
    // everywhere else it is user-visible (the device card, the hub's own
    // console). Free text rather than a picker: the peer list comes from the
    // hub's hub.peers topic, which needs the hub connected, and a device has
    // to stay configurable while nothing is plugged in.
    m_peerSourceIdEdit = new QLineEdit(connectionGroup);
    m_peerSourceIdEdit->setPlaceholderText(tr("e.g. 0x0A0A0A0A"));
    m_peerSourceIdEdit->setToolTip(
        tr("The robot's BTP source_id -- its permanent address, not the channel number the hub "
           "shows. That number is assigned in the order peers are first heard, so it changes "
           "when the hub reboots."));
    if (m_device.peerSourceId != 0) {
        m_peerSourceIdEdit->setText(
            QStringLiteral("0x%1").arg(m_device.peerSourceId, 8, 16, QLatin1Char('0')).toUpper());
    }
    m_peerSourceIdRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Robot source_id:"), m_peerSourceIdEdit);

    m_peerPasswordEdit = new QLineEdit(m_device.peerPassword, connectionGroup);
    m_peerPasswordEdit->setEchoMode(QLineEdit::Password);
    m_peerPasswordEdit->setToolTip(tr("Password for this robot's endpoint key."));
    m_peerPasswordRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Robot password:"), m_peerPasswordEdit);

    // Off by default, and that default is the point: a project file is
    // something people mail to each other and commit, so it must not become a
    // secrets file by accident. Opting in is per device, for the case where
    // the project lives on one machine and retyping every password on every
    // open buys nothing.
    m_cachePasswordCheck = new QCheckBox(tr("Save this password in the project file"), connectionGroup);
    m_cachePasswordCheck->setChecked(m_device.cachePeerPassword);
    m_cachePasswordCheck->setToolTip(
        tr("Anyone who opens the project file can read a saved password."));
    m_cachePasswordRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(QString(), m_cachePasswordCheck);

    m_statusLabel = new QLabel(
        m_device.connected ? tr("Connected") : tr("Disconnected"), connectionGroup);
    m_connectionLayout->addRow(tr("Status:"), m_statusLabel);

    updateTransportFieldsVisibility();

    // OTA group -- a separate Wi-Fi/HTTP channel (see device.h's
    // otaAddress comment), so unlike the Connection group above it doesn't
    // depend on transportType and is never hidden.
    auto* otaGroup = new QGroupBox(tr("OTA"), this);
    auto* otaLayout = new QFormLayout(otaGroup);
    m_otaAddressEdit = new QLineEdit(m_device.otaAddress, otaGroup);
    m_otaAddressEdit->setPlaceholderText(tr("e.g. robot1.local"));
    m_otaAddressEdit->setToolTip(
        tr("Hostname or IP the OTA tab uses for this device's firmware upload. "
           "Left blank, the device is listed there but nothing can be polled or uploaded."));
    otaLayout->addRow(tr("OTA address:"), m_otaAddressEdit);

    // Read-only: this is the last HELLO_RESULT the device sent, not
    // something a user should be able to type over (see Device::btpVersion/
    // btpId's own comments in devices/device.h). Empty until a session has
    // actually been established at least once.
    auto* reportedGroup = new QGroupBox(tr("Reported by device"), this);
    m_btpVersionEdit = new QLineEdit(m_device.btpVersion, reportedGroup);
    m_btpVersionEdit->setReadOnly(true);
    m_btpVersionEdit->setPlaceholderText(tr("(not connected yet)"));
    m_btpIdEdit = new QLineEdit(m_device.btpId, reportedGroup);
    m_btpIdEdit->setReadOnly(true);
    m_btpIdEdit->setPlaceholderText(tr("(not connected yet)"));
    auto* reportedLayout = new QFormLayout(reportedGroup);
    reportedLayout->addRow(tr("BTP version:"), m_btpVersionEdit);
    reportedLayout->addRow(tr("BTP ID:"), m_btpIdEdit);

    // What the device's own manifest (MANIFEST_DATA) announced -- every
    // (source, topic, schema_version) its Backend's TelemetryCatalog
    // currently holds, each with the human-readable name TELEMETRY.md
    // section 3 requires alongside the numeric topic_id. Read-only, same as
    // "Reported by device" above; a fixed max height keeps this from growing
    // the dialog unboundedly for a device with many topics -- QListWidget
    // scrolls internally past that instead.
    auto* catalogGroup = new QGroupBox(tr("Reported catalog"), this);
    m_catalogList = new QListWidget(catalogGroup);
    m_catalogList->setToolTip(tr("Hover an entry to see its fields."));
    m_catalogList->setMaximumHeight(140);
    auto* catalogLayout = new QVBoxLayout(catalogGroup);
    catalogLayout->addWidget(m_catalogList);
    setCatalogTopics({});

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(connectionGroup);
    layout->addWidget(otaGroup);
    layout->addWidget(reportedGroup);
    layout->addWidget(catalogGroup);
    layout->addSpacing(8);
    layout->addWidget(buttons);
}

Device DeviceConfigDialog::result() const {
    Device device = m_device;
    device.name = m_nameEdit->text();
    device.description = m_descriptionEdit->toPlainText();
    device.transportType = TransportType(m_transportTypeCombo->currentData().toInt());
    device.portName = m_portCombo->currentText();
    device.baudRate = m_baudCombo->currentText().toInt();
    device.lineTerminator = m_lineTerminatorCombo->currentData().toInt();
    device.usbPath = m_usbDeviceCombo->currentData().toString();
    device.parentDeviceId = m_parentCombo->currentData().toString();
    // Accepts 0x-prefixed hex or plain decimal; anything unparseable becomes
    // 0, which means "not configured" and never connects. Falling back to a
    // guess would be worse than refusing -- a child pointed at the wrong
    // robot plots real data from the wrong machine and raises no error.
    {
        const QString typed = m_peerSourceIdEdit->text().trimmed();
        bool parsed = false;
        const quint32 value = typed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                                  ? typed.mid(2).toUInt(&parsed, 16)
                                  : typed.toUInt(&parsed, 10);
        device.peerSourceId = parsed ? value : 0;
    }
    device.cachePeerPassword = m_cachePasswordCheck->isChecked();
    device.peerPassword = m_peerPasswordEdit->text();
    device.otaAddress = m_otaAddressEdit->text().trimmed();
    // otaPassword/cacheOtaPassword deliberately left as whatever m_device
    // already held -- this dialog doesn't edit them, the OTA tab does (see
    // OtaTab::passwordCacheChanged).
    // btpVersion/btpId deliberately left as whatever m_device already held --
    // "Reported by device" is read-only (see the fields' own declarations).
    return device;
}

void DeviceConfigDialog::updateTransportFieldsVisibility() {
    // Switched on the transport rather than on one boolean. With two
    // transports "serial or not" happened to be right; with three it is not --
    // treating Hub as "not serial" would offer it a USB device picker.
    const TransportType transport = TransportType(m_transportTypeCombo->currentData().toInt());
    const bool isSerial = transport == TransportType::Serial;
    const bool isUsbHid = transport == TransportType::UsbHid;
    const bool isHub = transport == TransportType::HubChannel;

    m_connectionLayout->setRowVisible(m_portRowIndex, isSerial);
    m_connectionLayout->setRowVisible(m_baudRowIndex, isSerial);
    m_connectionLayout->setRowVisible(m_lineTerminatorRowIndex, isSerial);
    m_connectionLayout->setRowVisible(m_usbDeviceRowIndex, isUsbHid);
    m_connectionLayout->setRowVisible(m_parentRowIndex, isHub);
    m_connectionLayout->setRowVisible(m_peerSourceIdRowIndex, isHub);
    m_connectionLayout->setRowVisible(m_peerPasswordRowIndex, isHub);
    m_connectionLayout->setRowVisible(m_cachePasswordRowIndex, isHub);
}

void DeviceConfigDialog::setAvailableParentDevices(const QVector<QPair<QString, QString>>& parents) {
    const QString current = m_parentCombo->currentData().toString();
    m_parentCombo->clear();
    // An unconfigured child is a valid state, so there has to be a way to say
    // "none", and it has to be where a fresh device lands.
    m_parentCombo->addItem(tr("(none)"), QString());
    for (const QPair<QString, QString>& parent : parents) {
        m_parentCombo->addItem(parent.second.isEmpty() ? parent.first : parent.second, parent.first);
    }
    const QString wanted = current.isEmpty() ? m_device.parentDeviceId : current;
    int index = m_parentCombo->findData(wanted);
    if (index < 0 && !wanted.isEmpty()) {
        // Configured but missing from the offered list (deleted, or no longer
        // a valid parent). Kept rather than silently repointed at "none" just
        // because someone opened the dialog.
        m_parentCombo->addItem(tr("%1 (unavailable)").arg(wanted), wanted);
        index = m_parentCombo->count() - 1;
    }
    m_parentCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void DeviceConfigDialog::setCatalogTopics(const QVector<CatalogTopicInfo>& topics) {
    m_catalogList->clear();
    if (topics.isEmpty()) {
        auto* placeholder = new QListWidgetItem(tr("(no topics reported yet)"), m_catalogList);
        placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
        return;
    }
    for (const CatalogTopicInfo& topic : topics) {
        auto* item = new QListWidgetItem(catalogTopicLine(topic), m_catalogList);
        item->setToolTip(catalogTopicTooltip(topic));
    }
}

void DeviceConfigDialog::setAvailablePorts(const QStringList& ports) {
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    m_portCombo->addItems(ports);
    if (!current.isEmpty() && m_portCombo->findText(current) < 0) {
        m_portCombo->insertItem(0, current);
    }
    m_portCombo->setCurrentText(current);
}

void DeviceConfigDialog::setAvailableUsbDevices(const QVector<UsbDeviceOption>& devices) {
    const QString currentPath = m_usbDeviceCombo->currentData().toString();
    m_usbDeviceCombo->clear();
    for (const UsbDeviceOption& device : devices) {
        m_usbDeviceCombo->addItem(device.label, device.path);
    }
    const int index = m_usbDeviceCombo->findData(currentPath);
    if (index >= 0) {
        m_usbDeviceCombo->setCurrentIndex(index);
    } else if (!currentPath.isEmpty()) {
        // Configured but not currently plugged in -- same "don't lose the
        // remembered target" treatment setAvailablePorts() gives a COM port
        // that's temporarily absent.
        m_usbDeviceCombo->insertItem(0, currentPath, currentPath);
        m_usbDeviceCombo->setCurrentIndex(0);
    }
}

} // namespace traceview
