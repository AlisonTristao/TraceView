#pragma once

#include <QDialog>

#include "devices/device.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

namespace traceview {

// Edits one Device: name/description directly, plus a "Reported by device"
// section (btpVersion/chipType/btpId) and a Connected checkbox. Constructed
// with the Device to edit, read back via result() after exec() returns
// Accepted -- follows AboutDialog/DonateDialog's construction convention
// (see core/aboutdialog.h) -- Q_OBJECT so tr() resolves this class as its own
// translation context.
class DeviceConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceConfigDialog(const Device& initial, QWidget* parent = nullptr);

    // Valid once exec() == QDialog::Accepted: the initial Device with every
    // editable field replaced by the dialog's current contents. id/commType
    // are carried over unchanged -- this dialog never reassigns either.
    Device result() const;

private:
    Device m_device;

    QLineEdit* m_nameEdit = nullptr;
    QPlainTextEdit* m_descriptionEdit = nullptr;
    // Stand-in for real connection state until the transport layer exists.
    QCheckBox* m_connectedCheck = nullptr;
    // These three become read-only once wired to a real BTP device manifest.
    QLineEdit* m_btpVersionEdit = nullptr;
    QLineEdit* m_chipTypeEdit = nullptr;
    QLineEdit* m_btpIdEdit = nullptr;
};

} // namespace traceview
