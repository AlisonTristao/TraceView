#include "propertiespanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>

#include "dashboard/widgetregistry.h"

namespace traceview {

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(kPropertiesPanelWidth);

    m_typeCombo = new QComboBox(this);
    m_nameEdit = new QLineEdit(this);
    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setPlaceholderText("(none)");
    m_keyEdit->setToolTip("Optional, must be unique — the handle future data updates will target this widget by.");

    auto* layout = new QFormLayout(this);
    layout->addRow("Type", m_typeCombo);
    layout->addRow("Name", m_nameEdit);
    layout->addRow("Key", m_keyEdit);

    setSelection(false, QString(), QString(), QString());

    connect(m_typeCombo, &QComboBox::activated, this, &PropertiesPanel::onTypeActivated);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &PropertiesPanel::onNameEditingFinished);
    connect(m_keyEdit, &QLineEdit::editingFinished, this, &PropertiesPanel::onKeyEditingFinished);
}

void PropertiesPanel::setAvailableTypes(const QVector<WidgetTypeInfo>& types) {
    m_types = types;

    const QSignalBlocker blocker(m_typeCombo);
    m_typeCombo->clear();
    for (const WidgetTypeInfo& info : m_types) {
        m_typeCombo->addItem(info.displayName);
    }
}

void PropertiesPanel::setSelection(bool hasSelection, const QString& typeId, const QString& name,
                                    const QString& key) {
    m_currentTypeId = typeId;
    m_currentName = name;
    m_currentKey = key;

    setEnabled(hasSelection);

    {
        const QSignalBlocker blocker(m_typeCombo);
        int index = -1;
        for (int i = 0; i < m_types.size(); ++i) {
            if (m_types[i].typeId == typeId) {
                index = i;
                break;
            }
        }
        m_typeCombo->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_nameEdit);
        m_nameEdit->setText(name);
    }
    {
        const QSignalBlocker blocker(m_keyEdit);
        m_keyEdit->setText(key);
    }
}

void PropertiesPanel::onTypeActivated(int index) {
    if (index < 0 || index >= m_types.size()) {
        return;
    }
    const QString typeId = m_types[index].typeId;
    if (typeId == m_currentTypeId) {
        return;
    }
    emit typeChangeRequested(typeId);
}

void PropertiesPanel::onNameEditingFinished() {
    const QString name = m_nameEdit->text().trimmed();
    if (name == m_currentName) {
        return;
    }
    emit nameChangeRequested(name);
}

void PropertiesPanel::onKeyEditingFinished() {
    const QString key = m_keyEdit->text().trimmed();
    if (key == m_currentKey) {
        return;
    }
    emit keyChangeRequested(key);
}

} // namespace traceview
