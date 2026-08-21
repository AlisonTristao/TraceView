#include "deviceconfigdialog.h"

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

    // Connection group -- port/baud/line-terminator, the config a
    // DeviceConnection (core/deviceconnection.h) actually opens with. Lives
    // here now, one per device, instead of the old single global Run tab bar.
    auto* connectionGroup = new QGroupBox(tr("Connection"), this);

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

    // Same list/default the old Run tab offered (extended for BTP v1 dongle
    // rates, see docs/PROTOCOL.md), now per-device rather than global.
    m_baudCombo = new QComboBox(connectionGroup);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600", "1000000",
                            "2000000", "3000000", "5000000"});
    m_baudCombo->setCurrentText(QString::number(m_device.baudRate));
    m_baudCombo->setToolTip(tr("Baud rate (type a custom value if yours isn't listed)"));
    m_baudCombo->setValidator(new QIntValidator(1, 10000000, m_baudCombo));

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

    m_statusLabel = new QLabel(
        m_device.connected ? tr("Connected") : tr("Disconnected"), connectionGroup);

    auto* connectionLayout = new QFormLayout(connectionGroup);
    connectionLayout->addRow(tr("Port:"), portRow);
    connectionLayout->addRow(tr("Baud rate:"), m_baudCombo);
    connectionLayout->addRow(tr("Line terminator:"), m_lineTerminatorCombo);
    connectionLayout->addRow(tr("Status:"), m_statusLabel);

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
    layout->addWidget(reportedGroup);
    layout->addWidget(catalogGroup);
    layout->addSpacing(8);
    layout->addWidget(buttons);
}

Device DeviceConfigDialog::result() const {
    Device device = m_device;
    device.name = m_nameEdit->text();
    device.description = m_descriptionEdit->toPlainText();
    device.portName = m_portCombo->currentText();
    device.baudRate = m_baudCombo->currentText().toInt();
    device.lineTerminator = m_lineTerminatorCombo->currentData().toInt();
    // btpVersion/btpId deliberately left as whatever m_device already held --
    // "Reported by device" is read-only (see the fields' own declarations).
    return device;
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

} // namespace traceview
