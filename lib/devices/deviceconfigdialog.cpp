#include "deviceconfigdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace traceview {

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

    // Visually separated so it's an obvious drop-in point once real BTP
    // manifest data exists -- see the read-only note on the member fields.
    auto* reportedGroup = new QGroupBox(tr("Reported by device"), this);
    m_btpVersionEdit = new QLineEdit(m_device.btpVersion, reportedGroup);
    m_chipTypeEdit = new QLineEdit(m_device.chipType, reportedGroup);
    m_btpIdEdit = new QLineEdit(m_device.btpId, reportedGroup);
    auto* reportedLayout = new QFormLayout(reportedGroup);
    reportedLayout->addRow(tr("BTP version:"), m_btpVersionEdit);
    reportedLayout->addRow(tr("Chip type:"), m_chipTypeEdit);
    reportedLayout->addRow(tr("BTP ID:"), m_btpIdEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(connectionGroup);
    layout->addWidget(reportedGroup);
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
    device.btpVersion = m_btpVersionEdit->text();
    device.chipType = m_chipTypeEdit->text();
    device.btpId = m_btpIdEdit->text();
    return device;
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
