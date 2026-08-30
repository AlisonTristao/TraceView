#include "deviceconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace traceview {

namespace {

// Renders one topic as an indented, JSON-like block -- e.g.:
//
//   motor_state
//     source: 0x11223344
//     topic: 0x0101
//     schema_version: 1
//     encoding: PACKED_LE
//     fields:
//       velocity [id=0x0003]: float32 (m/s)
//       position [id=0x0004]: float32
//
// Deliberately not actual JSON (no braces/quotes/commas): this is a
// read-only display meant to be scanned, not copy-pasted and parsed, so
// that punctuation would only add noise.
QString catalogTopicBlock(const CatalogTopicInfo& topic) {
    const QString label = topic.name.isEmpty() ? QObject::tr("(unnamed topic)") : topic.name;
    QStringList lines;
    lines << label;
    lines << QString("  source: 0x%1").arg(topic.sourceId, 8, 16, QChar('0'));
    lines << QString("  topic: 0x%1").arg(topic.topicId, 4, 16, QChar('0'));
    lines << QString("  schema_version: %1").arg(topic.schemaVersion);
    lines << QString("  encoding: %1").arg(topic.encoding);
    if (topic.fields.isEmpty()) {
        lines << QStringLiteral("  fields: (none declared)");
    } else {
        lines << QStringLiteral("  fields:");
        for (const CatalogTopicField& field : topic.fields) {
            // fieldId shown alongside the name always, not only when the
            // name is missing: the manifest's human-readable name is a
            // convenience TELEMETRY.md asks devices to provide, not a
            // guarantee it's stable or unambiguous, so the numeric id this
            // field is actually addressed by on the wire stays visible for
            // cross-checking either way.
            QString line = QString("    %1 [id=0x%2]: %3")
                               .arg(field.name.isEmpty() ? QObject::tr("(unnamed)") : field.name)
                               .arg(field.fieldId, 4, 16, QChar('0'))
                               .arg(field.type);
            if (!field.unit.isEmpty() && field.unit != QStringLiteral("1")) {
                line += QString(" (%1)").arg(field.unit);
            }
            lines << line;
        }
    }
    return lines.join('\n');
}

}  // namespace

DeviceConfigDialog::DeviceConfigDialog(const Device& initial, QWidget* parent)
    : QDialog(parent), m_device(initial) {
    setWindowTitle(tr("Device Settings"));
    // Wide enough for the two-column layout below (settings on the left,
    // reported catalog on the right) -- see the columns QHBoxLayout further
    // down.
    setMinimumWidth(680);

    m_nameEdit = new QLineEdit(m_device.name, this);
    m_nameEdit->setToolTip(
        tr("Shown as this device's title -- on its card in the Devices panel, and "
           "anywhere else it's picked from a list."));
    m_descriptionEdit = new QPlainTextEdit(m_device.description, this);
    // QPlainTextEdit is a QAbstractScrollArea: the mouse (and so the
    // ToolTip event) is actually over its viewport child widget, not this
    // frame, so setToolTip() here alone never shows -- has to go on the
    // viewport too.
    const QString descriptionTip =
        tr("Free-form notes about this device, shown on its card below the name.");
    m_descriptionEdit->setToolTip(descriptionTip);
    m_descriptionEdit->viewport()->setToolTip(descriptionTip);
    m_descriptionEdit->setFixedHeight(64);

    // Grouped like Connection/OTA/etc. below rather than left as a bare
    // QFormLayout -- otherwise Name/Description were the only fields in the
    // dialog without a group box around them.
    auto* generalGroup = new QGroupBox(tr("General"), this);
    auto* formLayout = new QFormLayout(generalGroup);
    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Description:"), m_descriptionEdit);

    // Connection group -- transport type plus whichever of
    // port/baud/line-terminator (Serial) or USB device (UsbHid) it needs,
    // the config a DeviceConnection (core/deviceconnection.h) actually opens
    // with. Lives here now, one per device, instead of the old single
    // global Run tab bar.
    auto* connectionGroup = new QGroupBox(tr("Connection"), this);
    m_connectionLayout = new QFormLayout(connectionGroup);
    // Label above field rather than beside it -- otherwise the label column's
    // width tracks whichever row set is currently visible, and switching
    // Transport (Serial/UsbHid/HubChannel show different rows via
    // updateTransportFieldsVisibility()) visibly changes this group's width
    // even with its height already pinned below.
    m_connectionLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);

    m_transportTypeCombo = new QComboBox(connectionGroup);
    m_transportTypeCombo->addItem(transportTypeLabel(TransportType::Serial),
                                  int(TransportType::Serial));
    m_transportTypeCombo->addItem(transportTypeLabel(TransportType::UsbHid),
                                  int(TransportType::UsbHid));
    m_transportTypeCombo->addItem(transportTypeLabel(TransportType::HubChannel),
                                  int(TransportType::HubChannel));
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
    m_refreshPortsButton->setText(QString::fromUtf8("\xE2\x9F\xB3"));  // ⟳
    m_refreshPortsButton->setToolTip(tr("Refresh port list"));
    m_refreshPortsButton->setAutoRaise(true);
    connect(m_refreshPortsButton, &QToolButton::clicked, this,
            &DeviceConfigDialog::refreshPortsRequested);

    auto* portRow = new QHBoxLayout;
    portRow->addWidget(m_portCombo, /*stretch=*/1);
    portRow->addWidget(m_refreshPortsButton);

    m_portRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Port:"), portRow);

    // Same list/default the old Run tab offered (extended for BTP v1 dongle
    // rates, see docs/PROTOCOL.md), now per-device rather than global.
    m_baudCombo = new QComboBox(connectionGroup);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800",
                           "921600", "1000000", "2000000", "3000000", "5000000"});
    m_baudCombo->setCurrentText(QString::number(m_device.baudRate));
    m_baudCombo->setToolTip(tr("Baud rate (type a custom value if yours isn't listed)"));
    m_baudCombo->setValidator(new QIntValidator(1, 10000000, m_baudCombo));
    m_baudRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Baud:"), m_baudCombo);

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
    m_lineTerminatorCombo->setToolTip(
        tr("Line terminator appended to control-widget commands sent to this device. "
           "Doesn't affect its serial terminal's raw keystrokes."));
    // Fixed rather than left to the form layout's AllNonFixedFieldsGrow
    // policy -- its longest item ("CRLF (\r\n)") is short, so growing it to
    // the full row width (matching Port/Baud/USB's more legitimately wide
    // fields) just left an empty-looking combo box.
    {
        QSizePolicy policy = m_lineTerminatorCombo->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Fixed);
        m_lineTerminatorCombo->setSizePolicy(policy);
    }
    m_lineTerminatorRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Terminator:"), m_lineTerminatorCombo);

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
    m_refreshUsbDevicesButton->setText(QString::fromUtf8("\xE2\x9F\xB3"));  // ⟳
    m_refreshUsbDevicesButton->setToolTip(tr("Refresh USB device list"));
    m_refreshUsbDevicesButton->setAutoRaise(true);
    connect(m_refreshUsbDevicesButton, &QToolButton::clicked, this,
            &DeviceConfigDialog::refreshUsbDevicesRequested);

    auto* usbDeviceRow = new QHBoxLayout;
    usbDeviceRow->addWidget(m_usbDeviceCombo, /*stretch=*/1);
    usbDeviceRow->addWidget(m_refreshUsbDevicesButton);
    m_usbDeviceRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("USB:"), usbDeviceRow);

    // Hub-channel rows -- shown instead of every row above when Transport is
    // set to Hub. A hub channel has no port and no baud rate: its "wire" is
    // another device's connection.
    m_parentCombo = new QComboBox(connectionGroup);
    // Bounded to a fixed content length rather than the default
    // AdjustToContentsOnFirstShow -- see m_peerSourceIdCombo's own comment
    // below for why: a long entry (a device with a long name here, a peer's
    // full label there) must not be able to widen the whole dialog.
    m_parentCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_parentCombo->setMinimumContentsLength(20);
    m_parentCombo->setToolTip(tr("The device whose connection carries this one."));
    if (!m_device.parentDeviceId.isEmpty()) {
        m_parentCombo->addItem(m_device.parentDeviceId, m_device.parentDeviceId);
    }
    m_parentRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Via:"), m_parentCombo);

    // Shown as hex because that is how a source_id appears everywhere else
    // it is user-visible (the device card, the hub's own console). Editable
    // combo, not a plain picker: the peer list comes live from the hub's own
    // hub.peers topic (see setAvailableHubPeers()), which needs the hub
    // connected and its manifest exchanged -- a device has to stay
    // configurable while nothing is plugged in yet, or for a robot the hub
    // hasn't heard.
    m_peerSourceIdCombo = new QComboBox(connectionGroup);
    m_peerSourceIdCombo->setEditable(true);
    // A peer's full label ("Ch 3 -- 0x0A0A0A0A, offline 12s [AA:BB:CC:DD:EE:FF]")
    // is long, and this dialog's default AdjustToContentsOnFirstShow policy
    // would otherwise let the combo (and with it the whole dialog) grow to
    // fit the longest one the hub happens to report -- exactly what made
    // Hub mode noticeably wider than Serial/USB. Fixed content length
    // instead: the box shows an elided prefix, the full text is still there
    // in the dropdown and the tooltip (see setAvailableHubPeers()).
    m_peerSourceIdCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_peerSourceIdCombo->setMinimumContentsLength(20);
    m_peerSourceIdCombo->setToolTip(
        tr("The robot's BTP source_id -- its permanent address, not the channel number the hub "
           "shows. Pick one the hub has actually heard (refreshed live while it's connected), or "
           "type a hex/decimal id by hand for a robot it hasn't heard yet."));
    m_peerSourceIdCombo->lineEdit()->setPlaceholderText(tr("e.g. 0x0A0A0A0A"));
    m_peerSourceId = m_device.peerSourceId;
    if (m_peerSourceId != 0) {
        m_peerSourceIdCombo->setCurrentText(
            QStringLiteral("0x%1").arg(m_peerSourceId, 8, 16, QLatin1Char('0')).toUpper());
    }
    connect(m_peerSourceIdCombo, &QComboBox::activated, this,
            [this](int index) { m_peerSourceId = m_peerSourceIdCombo->itemData(index).toUInt(); });
    connect(m_peerSourceIdCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        const QString typed = m_peerSourceIdCombo->currentText().trimmed();
        if (typed.isEmpty()) {
            // Explicit clear -- same "0 means not configured, never
            // connects" convention as every other field here (see
            // result()'s own comment).
            m_peerSourceId = 0;
            return;
        }
        bool parsed = false;
        const quint32 value = typed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                                  ? typed.mid(2).toUInt(&parsed, 16)
                                  : typed.toUInt(&parsed, 10);
        // Unparseable, non-empty text (most commonly a picked peer's label
        // still sitting there, untouched) is ignored rather than clearing a
        // previously valid binding -- same treatment
        // ChartConfigEditor/GaugeConfigEditor's Topic field gives a resolved
        // name that isn't meant to be typed back in.
        if (parsed) {
            m_peerSourceId = value;
        }
    });
    m_peerSourceIdRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Source ID:"), m_peerSourceIdCombo);

    // This device's OWN identity on the wire, not the robot's -- the value
    // `hub -bind <this>, <robot source_id>` needs on the dongle's shell.
    // Read-only and derived, never typed: the hub keys its bind table on it,
    // so it has to be stable across relaunches (hubChannelSourceId()'s own
    // comment, devices/device.h).
    m_childSourceIdLabel = new QLabel(connectionGroup);
    m_childSourceIdLabel->setText(
        QStringLiteral("0x%1").arg(hubChannelSourceId(m_device.id), 8, 16, QLatin1Char('0')).toUpper());
    m_childSourceIdLabel->setToolTip(
        tr("This device's own source_id. Pass it as the first argument to the dongle's "
           "\"hub -bind\" command, with the robot's Source ID above as the second, so the "
           "hub knows which robot this device's SUBSCRIBE/COMMAND traffic is for."));
    m_childSourceIdRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("This device's ID:"), m_childSourceIdLabel);

    m_peerPasswordEdit = new QLineEdit(m_device.peerPassword, connectionGroup);
    m_peerPasswordEdit->setEchoMode(QLineEdit::Password);
    m_peerPasswordEdit->setToolTip(tr("Password for this robot's endpoint key."));
    m_peerPasswordRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Password:"), m_peerPasswordEdit);

    // Off by default, and that default is the point: a project file is
    // something people mail to each other and commit, so it must not become a
    // secrets file by accident. Opting in is per device, for the case where
    // the project lives on one machine and retyping every password on every
    // open buys nothing.
    m_cachePasswordCheck =
        new QCheckBox(tr("Save this password in the project file"), connectionGroup);
    m_cachePasswordCheck->setChecked(m_device.cachePeerPassword);
    m_cachePasswordCheck->setToolTip(
        tr("Anyone who opens the project file can read a saved password."));
    m_cachePasswordRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(QString(), m_cachePasswordCheck);

    // Lock the Connection group to the tallest of the three transports' row
    // sets (Hub has the most: through-device/source_id/this-device's-id/
    // password/cache checkbox) instead of leaving it to shrink-wrap whichever
    // one happens
    // to be selected. Without this, switching the Transport combo hides/
    // shows rows via setRowVisible() and the whole dialog resizes itself
    // around it every time -- jarring, and it undoes the height the user set
    // by dragging the dialog. Measured by actually cycling through every
    // transport (rather than a hand-picked row count) so it stays correct
    // through font/style/translation changes that affect row heights.
    // updateTransportFieldsVisibility() is called directly rather than
    // relying on setCurrentIndex()'s currentIndexChanged signal -- if this
    // device's own transport happens to be the first type in the list below,
    // setCurrentIndex() on that same index is a no-op (Qt only emits the
    // signal on an actual change) and the very first measurement would
    // silently fall back to every row's default-visible state instead of
    // just that transport's.
    {
        int maxConnectionHeight = 0;
        for (TransportType type :
             {TransportType::Serial, TransportType::UsbHid, TransportType::HubChannel}) {
            const int index = m_transportTypeCombo->findData(int(type));
            m_transportTypeCombo->setCurrentIndex(index);
            updateTransportFieldsVisibility();
            m_connectionLayout->activate();
            maxConnectionHeight = qMax(maxConnectionHeight, connectionGroup->sizeHint().height());
        }
        connectionGroup->setMinimumHeight(maxConnectionHeight);
        // Restores the transport this device actually has.
        const int initialIndex = m_transportTypeCombo->findData(int(m_device.transportType));
        m_transportTypeCombo->setCurrentIndex(initialIndex >= 0 ? initialIndex : 0);
        updateTransportFieldsVisibility();
    }

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
    otaLayout->addRow(tr("Address:"), m_otaAddressEdit);

    m_otaPasswordEdit = new QLineEdit(m_device.otaPassword, otaGroup);
    m_otaPasswordEdit->setEchoMode(QLineEdit::Password);
    m_otaPasswordEdit->setToolTip(tr("Password for this device's X-OTA-Password header."));
    otaLayout->addRow(tr("Password:"), m_otaPasswordEdit);

    // Off by default, same reasoning as the Hub group's cache checkbox above:
    // a project file is something people mail to each other and commit.
    m_cacheOtaPasswordCheck = new QCheckBox(tr("Save this password in the project file"), otaGroup);
    m_cacheOtaPasswordCheck->setChecked(m_device.cacheOtaPassword);
    m_cacheOtaPasswordCheck->setToolTip(
        tr("Anyone who opens the project file can read a saved password."));
    otaLayout->addRow(QString(), m_cacheOtaPasswordCheck);

    // Read-only: this is the last HELLO_RESULT the device sent, not
    // something a user should be able to type over (see Device::btpVersion/
    // btpId's own comments in devices/device.h). Empty until a session has
    // actually been established at least once. Placed in the right column,
    // above "Reported catalog" below, rather than stacked into the left
    // column: both are read-only, device-reported info, and grouping them
    // together keeps the left column (settings the user actually edits)
    // shorter and roughly the same height as the right one.
    auto* reportedGroup = new QGroupBox(tr("Reported by device"), this);
    // Connect/disconnect state, not something HELLO_RESULT reports, but it's
    // still the device talking rather than a setting to edit -- grouped here
    // instead of its own row in Connection above, which only made that group
    // taller for one word of text.
    m_statusLabel =
        new QLabel(m_device.connected ? tr("Connected") : tr("Disconnected"), reportedGroup);
    m_btpVersionEdit = new QLineEdit(m_device.btpVersion, reportedGroup);
    m_btpVersionEdit->setReadOnly(true);
    m_btpVersionEdit->setPlaceholderText(tr("(not connected yet)"));
    m_btpIdEdit = new QLineEdit(m_device.btpId, reportedGroup);
    m_btpIdEdit->setReadOnly(true);
    m_btpIdEdit->setPlaceholderText(tr("(not connected yet)"));
    // The device's MANIFEST_DATA source_info block (BTP's docs/commands.md
    // section 3.12): firmware version, chip, running partition, a configured
    // name/description -- one "label: value" line each. A QLabel rather than a
    // form row per entry because the set is dynamic and arrives after the
    // dialog is built; selectable so an operator can copy a version string.
    m_reportedInfoLabel = new QLabel(reportedGroup);
    m_reportedInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reportedInfoLabel->setWordWrap(true);
    auto* reportedLayout = new QFormLayout(reportedGroup);
    reportedLayout->addRow(tr("Status:"), m_statusLabel);
    reportedLayout->addRow(tr("Version:"), m_btpVersionEdit);
    reportedLayout->addRow(tr("ID:"), m_btpIdEdit);
    reportedLayout->addRow(tr("Info:"), m_reportedInfoLabel);
    setReportedInfo(m_device.reportedInfo);

    // What the device's own manifest (MANIFEST_DATA) announced -- every
    // (source, topic, schema_version) its Backend's TelemetryCatalog
    // currently holds, each with the human-readable name TELEMETRY.md
    // section 3 requires alongside the numeric topic_id, and its field list
    // nested underneath (see catalogTopicBlock() above). Read-only, same as
    // "Reported by device" above; lives in its own scrolling right column
    // (see the columns QHBoxLayout below) instead of a fixed max height, so
    // a device with many topics doesn't push the dialog's buttons down.
    auto* catalogGroup = new QGroupBox(tr("Reported catalog"), this);
    m_catalogList = new QPlainTextEdit(catalogGroup);
    m_catalogList->setReadOnly(true);
    QFont catalogFont(QStringLiteral("Monospace"));
    catalogFont.setStyleHint(QFont::Monospace);
    m_catalogList->setFont(catalogFont);
    auto* catalogLayout = new QVBoxLayout(catalogGroup);
    catalogLayout->addWidget(m_catalogList);
    setCatalogTopics({});

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // ActionRole, not Ok/Apply -- this must not close the dialog. It lets the
    // port/baud/etc. above be applied and a (re)connect attempted right
    // away, with "Reported by device"/"Reported catalog" refreshing in place
    // as the result comes back, instead of requiring OK-then-reopen for
    // every port guess.
    QPushButton* connectButton = buttons->addButton(tr("Connect"), QDialogButtonBox::ActionRole);
    connectButton->setToolTip(
        tr("Apply the settings above and (re)connect now, without closing this dialog."));
    connect(connectButton, &QPushButton::clicked, this, &DeviceConfigDialog::applyRequested);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Two columns: settings the user edits on the left, everything the
    // device itself reports on the right -- "Reported by device" stacked
    // above "Reported catalog" instead of piling onto the left column, which
    // both shortens the left column and keeps the two columns closer in
    // height.
    auto* leftColumn = new QVBoxLayout;
    leftColumn->addWidget(generalGroup);
    leftColumn->addWidget(connectionGroup);
    leftColumn->addWidget(otaGroup);
    leftColumn->addStretch();

    auto* rightColumn = new QVBoxLayout;
    rightColumn->addWidget(reportedGroup);
    rightColumn->addWidget(catalogGroup, /*stretch=*/1);

    // Right column's own sizeHint (just reportedGroup's few short rows plus
    // whatever the catalog box asks for) is naturally narrower than the left
    // one's, which is widened by its labeled rows and width-bound combos
    // (m_peerSourceIdCombo/m_parentCombo). The equal stretch factors below
    // only redistribute space *beyond* each column's own sizeHint when the
    // dialog is grown past it -- they don't equalize the two up front. Pin
    // both right-column group boxes to the left column's natural width
    // (left stays exactly as it already sizes itself) so "Reported catalog"
    // starts out the same width as the settings column instead of visibly
    // narrower.
    const int leftColumnWidth = leftColumn->sizeHint().width();
    reportedGroup->setMinimumWidth(leftColumnWidth);
    catalogGroup->setMinimumWidth(leftColumnWidth);

    auto* columns = new QHBoxLayout;
    columns->addLayout(leftColumn, /*stretch=*/1);
    columns->addLayout(rightColumn, /*stretch=*/1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addSpacing(8);
    layout->addWidget(buttons);

    // Sets the dialog's initial size to fit whichever transport is selected
    // above. Connection's height is now pinned to Hub's row set regardless
    // (see the setMinimumHeight() call above), so later Transport switches
    // don't change sizeHint() and this call is never needed again after
    // construction. Width doesn't need any such pinning: every row's field
    // spans the full row width (WrapAllRows, further up) regardless of which
    // rows are visible, and the two combos whose *content* could otherwise
    // widen the dialog (m_parentCombo, m_peerSourceIdCombo) are bounded to a
    // fixed content length regardless of what's actually loaded into them.
    resize(sizeHint());
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
    // m_peerSourceId is the ground truth (see its own declaration) --
    // whichever of picking a peer or typing a hex/decimal id last actually
    // parsed successfully. Never re-derived from the combo's displayed text
    // here: a picked entry's label ("Channel 0 -- 0x0A0A0A0A (online)") isn't
    // meant to be parsed back.
    device.peerSourceId = m_peerSourceId;
    device.cachePeerPassword = m_cachePasswordCheck->isChecked();
    device.peerPassword = m_peerPasswordEdit->text();
    device.otaAddress = m_otaAddressEdit->text().trimmed();
    device.otaPassword = m_otaPasswordEdit->text();
    device.cacheOtaPassword = m_cacheOtaPasswordCheck->isChecked();
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
    m_connectionLayout->setRowVisible(m_childSourceIdRowIndex, isHub);
    m_connectionLayout->setRowVisible(m_peerPasswordRowIndex, isHub);
    m_connectionLayout->setRowVisible(m_cachePasswordRowIndex, isHub);
}

void DeviceConfigDialog::setAvailableParentDevices(
    const QVector<QPair<QString, QString>>& parents) {
    const QString current = m_parentCombo->currentData().toString();
    m_parentCombo->clear();
    // An unconfigured child is a valid state, so there has to be a way to say
    // "none", and it has to be where a fresh device lands.
    m_parentCombo->addItem(tr("(none)"), QString());
    for (const QPair<QString, QString>& parent : parents) {
        m_parentCombo->addItem(parent.second.isEmpty() ? parent.first : parent.second,
                               parent.first);
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
    if (topics.isEmpty()) {
        m_catalogList->setPlainText(tr("(no topics reported yet)"));
        return;
    }
    QStringList blocks;
    blocks.reserve(topics.size());
    for (const CatalogTopicInfo& topic : topics) {
        blocks << catalogTopicBlock(topic);
    }
    m_catalogList->setPlainText(blocks.join("\n\n"));
}

void DeviceConfigDialog::setReportedIdentity(const QString& btpVersion, const QString& btpId) {
    m_device.btpVersion = btpVersion;
    m_device.btpId = btpId;
    m_btpVersionEdit->setText(btpVersion);
    m_btpIdEdit->setText(btpId);
}

void DeviceConfigDialog::setReportedInfo(const QVector<DeviceInfoRecord>& info) {
    m_device.reportedInfo = info;
    if (info.isEmpty()) {
        m_reportedInfoLabel->setText(tr("(nothing reported yet)"));
        return;
    }
    QStringList lines;
    lines.reserve(info.size());
    for (const DeviceInfoRecord& entry : info) {
        // label is optional on the wire (commands.md 3.12) -- fall back to the
        // machine key so the row is never blank.
        const QString caption = entry.label.isEmpty() ? entry.key : entry.label;
        lines.append(QStringLiteral("%1: %2").arg(caption, entry.value));
    }
    m_reportedInfoLabel->setText(lines.join(QLatin1Char('\n')));
}

void DeviceConfigDialog::setConnectionStatus(bool connected) {
    m_device.connected = connected;
    m_statusLabel->setText(connected ? tr("Connected") : tr("Disconnected"));
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

void DeviceConfigDialog::setAvailableHubPeers(const QVector<HubPeer>& peers) {
    const QString currentText = m_peerSourceIdCombo->currentText();
    m_peerSourceIdCombo->clear();
    for (const HubPeer& peer : peers) {
        const QString hex =
            QStringLiteral("0x%1").arg(peer.sourceId, 8, 16, QLatin1Char('0')).toUpper();
        // Kept short -- see m_peerSourceIdCombo's own comment on why a long
        // item label isn't wanted here. The MAC (when reported) goes in the
        // item's tooltip instead of getting appended to the label.
        const QString status =
            peer.online ? tr("online") : tr("offline %1s").arg(peer.lastSeenAgeMs / 1000);
        const QString label = tr("Ch %1 -- %2, %3").arg(peer.channel).arg(hex, status);
        m_peerSourceIdCombo->addItem(label, peer.sourceId);
        if (!peer.mac.isEmpty()) {
            m_peerSourceIdCombo->setItemData(m_peerSourceIdCombo->count() - 1, peer.mac,
                                             Qt::ToolTipRole);
        }
    }
    m_peerSourceIdCombo->setCurrentText(currentText);
}

QString DeviceConfigDialog::currentParentDeviceId() const {
    return m_parentCombo->currentData().toString();
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

}  // namespace traceview
