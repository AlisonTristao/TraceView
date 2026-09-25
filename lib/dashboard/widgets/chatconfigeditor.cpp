#include "chatconfigeditor.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLineEdit>

namespace traceview {

ChatConfigEditor::ChatConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_userNameEdit = new QLineEdit(this);
    m_userNameEdit->setPlaceholderText(tr("You"));

    m_ownOnRightCheck = new QCheckBox(tr("Show my messages on the right"), this);
    m_ownOnRightCheck->setChecked(true);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(tr("User name"), m_userNameEdit);
    layout->addRow(QString(), m_ownOnRightCheck);

    connect(m_userNameEdit, &QLineEdit::editingFinished, this, &ChatConfigEditor::emitChanged);
    connect(m_ownOnRightCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
}

void ChatConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_userNameEdit->setText(config.value("userName").toString());
    m_ownOnRightCheck->setChecked(config.value("ownMessagesOnRight").toBool(true));
    m_updating = false;
}

QJsonObject ChatConfigEditor::config() const {
    QJsonObject config;
    config["userName"] = m_userNameEdit->text().trimmed();
    config["ownMessagesOnRight"] = m_ownOnRightCheck->isChecked();
    return config;
}

void ChatConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

}  // namespace traceview
