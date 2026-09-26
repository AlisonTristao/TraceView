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
#include <QSpinBox>
#include <QScrollArea>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include "devices/devicelinkstable.h"
#include "devices/robotlinkspanel.h"

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
// Stand-in for QFormLayout::setRowVisible(), which only exists from Qt 6.4 --
// Ubuntu 22.04's packaged Qt is 6.2. Hides/shows the label and field of a row,
// descending into a field that is itself a layout (e.g. a combo box paired
// with a refresh button, see m_portRowIndex/m_usbDeviceRowIndex).
void setFormRowVisible(QFormLayout* layout, int row, bool visible) {
    const auto setItemVisible = [visible](QLayoutItem* item) {
        if (!item) return;
        if (QWidget* widget = item->widget()) {
            widget->setVisible(visible);
        } else if (QLayout* childLayout = item->layout()) {
            for (int i = 0; i < childLayout->count(); ++i) {
                if (QWidget* childWidget = childLayout->itemAt(i)->widget()) {
                    childWidget->setVisible(visible);
                }
            }
        }
    };
    setItemVisible(layout->itemAt(row, QFormLayout::LabelRole));
    setItemVisible(layout->itemAt(row, QFormLayout::FieldRole));
}

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
    // The card title is an override of the robot's name, so it lives with
    // the connection's advanced fields below. There is no description to
    // edit: the card shows what the device reports about itself instead.
    m_nameEdit->hide();

    // Connection group -- every way to reach this device, as one table (row 1
    // is the primary link, the rest Device::extraLinks, tried in that order;
    // see DeviceLinksTable), then what is the DEVICE's rather than any one
    // link's: the channel-B password (the robot's key -- the same for TCP,
    // BLE and a hub channel), the command line terminator, and this device's
    // own id for the dongle's "hub -bind".
    auto* connectionGroup = new QGroupBox(tr("Connection"), this);
    auto* connectionLayout = new QVBoxLayout(connectionGroup);

    QVector<DeviceLink> links;
    links.append(deviceLinkAt(m_device, 0));
    links += m_device.extraLinks;
    // A fresh device's primary link is a blank placeholder (Serial, no port),
    // not something the user chose: shown as no link at all, so the simple
    // view starts with every box unticked.
    if (!links.first().autoTarget && !deviceLinkConfigured(links.first())) {
        links.removeFirst();
    }

    // Simple view: one robot name, one checkbox per kind of link, each
    // defaulting to Automatic (found by that name). See RobotLinksPanel.
    m_linksPanel = new RobotLinksPanel(
        m_device.robotName.isEmpty() ? m_device.name : m_device.robotName, links, connectionGroup);
    connectionLayout->addWidget(m_linksPanel);
    connect(m_linksPanel, &RobotLinksPanel::refreshPortsRequested, this,
            &DeviceConfigDialog::refreshPortsRequested);
    connect(m_linksPanel, &RobotLinksPanel::scanBleRequested, this,
            &DeviceConfigDialog::scanBleRequested);
    for (const DeviceInfoRecord& entry : m_device.reportedInfo) {
        if (entry.key == QLatin1String("name")) {
            m_linksPanel->addRobotNameCandidate(entry.value);
        }
    }

    // Advanced view: the same links as a table -- order, baud rate, TCP port,
    // hand-typed targets, USB HID. Collapsed unless the device already uses
    // something only the table can show.
    m_advancedButton = new QToolButton(connectionGroup);
    m_advancedButton->setText(tr("Advanced"));
    m_advancedButton->setCheckable(true);
    m_advancedButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedButton->setAutoRaise(true);
    m_advancedButton->setToolTip(
        tr("Every link as a table: the order they are tried in, baud rate, TCP port, "
           "and targets typed by hand."));
    connectionLayout->addWidget(m_advancedButton);

    m_linksTable = new DeviceLinksTable(links, connectionGroup);
    connectionLayout->addWidget(m_linksTable, /*stretch=*/1);
    connect(m_advancedButton, &QToolButton::toggled, this, [this](bool on) {
        m_advancedButton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        m_linksTable->setVisible(on);
        updateConnectionRows();
    });
    bool needsTable = false;
    QVector<TransportType> seenTypes;
    for (const DeviceLink& link : links) {
        needsTable = needsTable || link.transportType == TransportType::UsbHid ||
                     (!link.autoTarget && deviceLinkConfigured(link)) ||
                     !link.enabled || seenTypes.contains(link.transportType);
        seenTypes.append(link.transportType);
    }
    m_advancedButton->setChecked(needsTable);
    m_advancedButton->setArrowType(needsTable ? Qt::DownArrow : Qt::RightArrow);
    m_linksTable->setVisible(needsTable);

    // The two views edit one list: whichever was edited pushes it to the other.
    connect(m_linksPanel, &RobotLinksPanel::linksEdited, this,
            [this](const QVector<DeviceLink>& edited) {
                m_linksTable->setLinks(edited);
                updateConnectionRows();
            });
    connect(m_linksTable, &DeviceLinksTable::linksChanged, this,
            [this] { m_linksPanel->setLinks(m_linksTable->links()); });
    connect(m_linksTable, &DeviceLinksTable::refreshPortsRequested, this,
            &DeviceConfigDialog::refreshPortsRequested);
    connect(m_linksTable, &DeviceLinksTable::refreshUsbDevicesRequested, this,
            &DeviceConfigDialog::refreshUsbDevicesRequested);
    connect(m_linksTable, &DeviceLinksTable::scanBleRequested, this,
            &DeviceConfigDialog::scanBleRequested);
    connect(m_linksTable, &DeviceLinksTable::linksChanged, this,
            &DeviceConfigDialog::updateConnectionRows);

    m_connectionLayout = new QFormLayout();
    m_connectionLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    connectionLayout->addLayout(m_connectionLayout);
    m_connectionLayout->addRow(tr("Card title:"), m_nameEdit);

    m_peerPasswordEdit = new QLineEdit(m_device.peerPassword, connectionGroup);
    m_peerPasswordEdit->setEchoMode(QLineEdit::Password);
    m_peerPasswordEdit->setToolTip(
        tr("The robot's channel-B password. One per device: every TCP, BLE and hub connection "
           "above uses it."));
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

    // Values match traceview::LineTerminator's ordinals (core/serialtransport.h)
    // -- Device::lineTerminator stores that same ordinal as a plain int since
    // this module can't depend on that enum's header (see device.h). Only
    // meaningful over a serial console, so shown only when a Serial row exists.
    m_lineTerminatorCombo = new QComboBox(connectionGroup);
    m_lineTerminatorCombo->addItem(tr("None"), 0);
    m_lineTerminatorCombo->addItem(tr("LF (\\n)"), 1);
    m_lineTerminatorCombo->addItem(tr("CR (\\r)"), 2);
    m_lineTerminatorCombo->addItem(tr("CRLF (\\r\\n)"), 3);
    const int terminatorIndex = m_lineTerminatorCombo->findData(m_device.lineTerminator);
    m_lineTerminatorCombo->setCurrentIndex(terminatorIndex >= 0 ? terminatorIndex : 1);
    m_lineTerminatorCombo->setToolTip(
        tr("Line terminator appended to control-widget commands sent over serial. "
           "Doesn't affect its serial terminal's raw keystrokes."));
    m_lineTerminatorRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("Terminator:"), m_lineTerminatorCombo);

    // This device's OWN identity on the wire, not the robot's -- the value
    // `hub -bind <this>, <robot source_id>` needs on the dongle's shell.
    // Read-only and derived, never typed: the hub keys its bind table on it,
    // so it has to be stable across relaunches (hubChannelSourceId()'s own
    // comment, devices/device.h). Shown only when a Hub row exists.
    m_childSourceIdLabel = new QLabel(connectionGroup);
    m_childSourceIdLabel->setText(
        QStringLiteral("0x%1").arg(hubChannelSourceId(m_device.id), 8, 16, QLatin1Char('0')).toUpper());
    m_childSourceIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_childSourceIdLabel->setToolTip(
        tr("This device's own source_id. Pass it as the first argument to the hub's "
           "\"hub -bind\" command, with the robot's id as the second, so the hub knows which "
           "robot this device's SUBSCRIBE/COMMAND traffic is for."));
    m_childSourceIdRowIndex = m_connectionLayout->rowCount();
    m_connectionLayout->addRow(tr("This device's ID:"), m_childSourceIdLabel);
    updateConnectionRows();

    // OTA group -- a separate Wi-Fi/HTTP channel (see device.h's
    // otaAddress comment), independent of how the device is connected.
    auto* otaGroup = new QGroupBox(tr("OTA"), this);
    auto* otaLayout = new QFormLayout(otaGroup);
    m_otaAddressEdit = new QLineEdit(m_device.otaAddress, otaGroup);
    m_otaAddressEdit->setPlaceholderText(tr("e.g. robot1.local"));
    m_otaAddressEdit->setToolTip(
        tr("Hostname or IP the OTA tab uses for this device's firmware upload. "
           "Left blank, the device is listed there but nothing can be polled or uploaded."));
    // The device announces its own OTA hostname in the source_info block (key
    // "ota_endpoint", commands.md 3.12). This offers it as a one-click fill --
    // never a silent overwrite, since the field is persisted config and the
    // user may have set a proxy/IP/mirror on purpose. Hidden until a reported
    // endpoint exists and differs from what's typed.
    m_useReportedOtaButton = new QToolButton(otaGroup);
    m_useReportedOtaButton->setText(tr("Use reported"));
    m_useReportedOtaButton->setToolTip(tr("Fill in the address the device reported for itself."));
    m_useReportedOtaButton->hide();
    connect(m_useReportedOtaButton, &QToolButton::clicked, this, [this]() {
        m_otaAddressEdit->setText(m_reportedOtaEndpoint);
    });
    connect(m_otaAddressEdit, &QLineEdit::textChanged, this,
            [this]() { updateReportedOtaHint(); });
    auto* otaAddressRow = new QHBoxLayout();
    otaAddressRow->setContentsMargins(0, 0, 0, 0);
    otaAddressRow->addWidget(m_otaAddressEdit);
    otaAddressRow->addWidget(m_useReportedOtaButton);
    otaLayout->addRow(tr("Address:"), otaAddressRow);

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
    // between OTA and "Reported catalog", rather than stacked into the left
    // column: keeps the left column (the connection settings) short and
    // roughly the same height as the right one.
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

    // Two columns: the connection settings on the left, OTA plus everything
    // the device itself reports on the right -- OTA sits above "Reported by
    // device"/"Reported catalog" instead of under the connection group,
    // which keeps the left column (and so the dialog) from growing too tall.
    auto* leftColumn = new QVBoxLayout;
    leftColumn->addWidget(connectionGroup);
    leftColumn->addStretch();

    auto* rightColumn = new QVBoxLayout;
    rightColumn->addWidget(otaGroup);
    rightColumn->addWidget(reportedGroup);
    rightColumn->addWidget(catalogGroup, /*stretch=*/1);

    // Contain changing size hints (discovery, manifest, advanced fields)
    // inside a scroll area rather than growing the native dialog.
    auto* content = new QWidget(this);
    auto* columns = new QHBoxLayout(content);
    columns->addLayout(leftColumn, /*stretch=*/1);
    columns->addLayout(rightColumn, /*stretch=*/1);

    auto* layout = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    layout->addSpacing(8);
    layout->addWidget(buttons);

    // The connections table's combos are bounded to a fixed content length
    // (DeviceLinksTable's boundedCombo()), so no port label, BLE name or peer
    // label can widen the dialog past this.
    resize(QSize(1000, 720).boundedTo(screen()->availableGeometry().size() - QSize(40, 60)));
}

Device DeviceConfigDialog::result() const {
    Device device = m_device;
    device.robotName = m_linksPanel->robotName();
    // No title of its own: the robot's name is the obvious one.
    const QString title = m_nameEdit->text().trimmed();
    device.name = title.isEmpty() || title == m_device.robotName ||
                          (m_device.robotName.isEmpty() && title == m_device.name)
                      ? device.robotName : title;
    // Row 1 of the connections table is the primary link (the Device's own
    // transport fields); the rest become extraLinks. An empty table leaves a
    // default, unconfigured primary -- same "never connects" state a fresh
    // device starts in.
    QVector<DeviceLink> links = m_linksTable->links();
    // The legacy primary slot has no enabled flag. Put the first enabled
    // link there, or leave it unconfigured when every transport is disabled.
    DeviceLink primary;
    for (int i = 0; i < links.size(); ++i) {
        if (links.at(i).enabled) {
            primary = links.takeAt(i);
            break;
        }
    }
    device = deviceWithLink(device, primary);
    device.extraLinks = links;
    device.lineTerminator = m_lineTerminatorCombo->currentData().toInt();
    device.cachePeerPassword = m_cachePasswordCheck->isChecked();
    device.peerPassword = m_peerPasswordEdit->text();
    device.otaAddress = m_otaAddressEdit->text().trimmed();
    device.otaPassword = m_otaPasswordEdit->text();
    device.cacheOtaPassword = m_cacheOtaPasswordCheck->isChecked();
    // btpVersion/btpId deliberately left as whatever m_device already held --
    // "Reported by device" is read-only (see the fields' own declarations).
    return device;
}

void DeviceConfigDialog::updateConnectionRows() {
    if (!m_connectionLayout) return;
    const bool advanced = m_advancedButton->isChecked();
    setFormRowVisible(m_connectionLayout, 0, advanced);
    const bool anyDirectOrHub = m_linksTable->hasLinkOfType(TransportType::Tcp) ||
                                m_linksTable->hasLinkOfType(TransportType::Ble) ||
                                m_linksTable->hasLinkOfType(TransportType::HubChannel);
    // A serial/USB-only device talks to a dongle's console, which has no
    // channel-B key -- no password to ask for.
    setFormRowVisible(m_connectionLayout, m_peerPasswordRowIndex, anyDirectOrHub);
    setFormRowVisible(m_connectionLayout, m_cachePasswordRowIndex, anyDirectOrHub);
    setFormRowVisible(m_connectionLayout, m_lineTerminatorRowIndex,
                      advanced && m_linksTable->hasLinkOfType(TransportType::Serial));
    setFormRowVisible(m_connectionLayout, m_childSourceIdRowIndex,
                      advanced && m_linksTable->hasLinkOfType(TransportType::HubChannel));
}

void DeviceConfigDialog::setAvailableParentDevices(
    const QVector<QPair<QString, QString>>& parents) {
    m_linksTable->setAvailableParentDevices(parents);
    m_linksPanel->setAvailableParentDevices(parents);
}

void DeviceConfigDialog::setCatalogTopics(const QVector<CatalogTopicInfo>& topics) {
    m_liveTopics = topics;
    renderCatalog();
}

void DeviceConfigDialog::setCachedDescription(const QVector<CatalogTopicInfo>& topics,
                                              const QVector<DeviceInfoRecord>& info) {
    m_cachedTopics = topics;
    m_cachedInfo = info;
    // The robot's id as the placeholder of an empty ID field: HELLO_RESULT
    // gives it live, but a cached manifest is one source's, so it is known.
    const QString cachedId =
        topics.isEmpty()
            ? QString()
            : QStringLiteral("0x%1").arg(
                  QStringLiteral("%1").arg(topics.first().sourceId, 8, 16, QChar('0')).toUpper());
    m_btpIdEdit->setPlaceholderText(cachedId.isEmpty() ? tr("(not connected yet)")
                                                       : tr("%1 (last connection)").arg(cachedId));
    renderCatalog();
    renderReportedInfo();
}

void DeviceConfigDialog::renderCatalog() {
    const bool fromCache = m_liveTopics.isEmpty() && !m_cachedTopics.isEmpty();
    const QVector<CatalogTopicInfo>& topics = fromCache ? m_cachedTopics : m_liveTopics;
    if (topics.isEmpty()) {
        m_catalogList->setPlainText(tr("(no topics reported yet)"));
        return;
    }
    QStringList blocks;
    blocks.reserve(topics.size() + 1);
    if (fromCache) {
        blocks << tr("(saved from the last connection)");
    }
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
    renderReportedInfo();
}

void DeviceConfigDialog::renderReportedInfo() {
    const bool fromCache = m_device.reportedInfo.isEmpty() && !m_cachedInfo.isEmpty();
    const QVector<DeviceInfoRecord>& info = fromCache ? m_cachedInfo : m_device.reportedInfo;

    QString reportedOta;
    QStringList lines;
    lines.reserve(info.size() + 1);
    if (fromCache) {
        lines.append(tr("(saved from the last connection)"));
    }
    for (const DeviceInfoRecord& entry : info) {
        // label is optional on the wire (commands.md 3.12) -- fall back to the
        // machine key so the row is never blank.
        const QString caption = entry.label.isEmpty() ? entry.key : entry.label;
        lines.append(QStringLiteral("%1: %2").arg(caption, entry.value));
        if (entry.key == QLatin1String("ota_endpoint")) {
            reportedOta = entry.value.trimmed();
        }
        if (entry.key == QLatin1String("name") && m_linksPanel != nullptr) {
            m_linksPanel->addRobotNameCandidate(entry.value);
        }
    }
    m_reportedInfoLabel->setText(info.isEmpty() ? tr("(nothing reported yet)")
                                                : lines.join(QLatin1Char('\n')));

    m_reportedOtaEndpoint = reportedOta;
    updateReportedOtaHint();
}

void DeviceConfigDialog::updateReportedOtaHint() {
    if (m_useReportedOtaButton == nullptr) {
        return;
    }
    if (m_reportedOtaEndpoint.isEmpty()) {
        m_otaAddressEdit->setPlaceholderText(tr("e.g. robot1.local"));
        m_useReportedOtaButton->hide();
        return;
    }
    m_otaAddressEdit->setPlaceholderText(tr("device reports: %1").arg(m_reportedOtaEndpoint));
    // Offer the fill only when it would actually change something.
    m_useReportedOtaButton->setVisible(m_otaAddressEdit->text().trimmed() != m_reportedOtaEndpoint);
}

void DeviceConfigDialog::setConnectionStatus(bool connected) {
    m_device.connected = connected;
    m_statusLabel->setText(connected ? tr("Connected") : tr("Disconnected"));
}

void DeviceConfigDialog::setAvailablePorts(const QVector<SerialPortOption>& ports) {
    m_linksTable->setAvailablePorts(ports);
    m_linksPanel->setAvailablePorts(ports);
}

void DeviceConfigDialog::addDiscoveredBleDevice(const QString& name, const QString& address) {
    m_linksTable->addDiscoveredBleDevice(name, address);
    m_linksPanel->addDiscoveredBleDevice(name, address);
}

void DeviceConfigDialog::setAvailableHubPeers(const QVector<HubPeer>& peers) {
    m_linksTable->setAvailableHubPeers(peers);
    m_linksPanel->setAvailableHubPeers(peers);
}

QString DeviceConfigDialog::currentParentDeviceId() const {
    return m_linksTable->hubParentForPeers();
}

void DeviceConfigDialog::setAvailableUsbDevices(const QVector<UsbDeviceOption>& devices) {
    m_linksTable->setAvailableUsbDevices(devices);
}

}  // namespace traceview
