#include "deviceconfigdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
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

    m_connectedCheck = new QCheckBox(tr("Connected"), this);
    m_connectedCheck->setChecked(m_device.connected);

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
    layout->addWidget(m_connectedCheck);
    layout->addWidget(reportedGroup);
    layout->addSpacing(8);
    layout->addWidget(buttons);
}

Device DeviceConfigDialog::result() const {
    Device device = m_device;
    device.name = m_nameEdit->text();
    device.description = m_descriptionEdit->toPlainText();
    device.connected = m_connectedCheck->isChecked();
    device.btpVersion = m_btpVersionEdit->text();
    device.chipType = m_chipTypeEdit->text();
    device.btpId = m_btpIdEdit->text();
    return device;
}

} // namespace traceview
