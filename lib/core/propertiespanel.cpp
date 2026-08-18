#include "propertiespanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "dashboard/widgetregistry.h"

namespace traceview {

PropertiesPanel::PropertiesPanel(QWidget* parent) : DockablePanel(parent) {
    setObjectName("propertiesPanel");

    m_typeCombo = new QComboBox();
    m_nameEdit = new QLineEdit();
    m_keyEdit = new QLineEdit();
    m_keyEdit->setPlaceholderText(tr("(none)"));
    m_keyEdit->setToolTip(tr("Optional, must be unique — the handle future data updates will target this widget by."));

    auto* formLayout = new QFormLayout();
    formLayout->addRow(tr("Type"), m_typeCombo);
    formLayout->addRow(tr("Name"), m_nameEdit);
    formLayout->addRow(tr("Key"), m_keyEdit);

    m_divider = new QFrame();
    m_divider->setObjectName("sectionDivider");
    m_divider->setFrameShape(QFrame::HLine);
    m_divider->setFixedHeight(1);

    // The config editor swapped in by ensureConfigEditor() is the sole
    // (removable) child of this layout, kept top-anchored by the trailing
    // stretch so a short editor (or none) doesn't stretch to fill the panel.
    m_configContainer = new QWidget();
    m_configLayout = new QVBoxLayout(m_configContainer);
    m_configLayout->setContentsMargins(0, 0, 0, 0);
    m_configLayout->addStretch(1);

    m_configScrollArea = new QScrollArea();
    m_configScrollArea->setWidgetResizable(true);
    m_configScrollArea->setFrameShape(QFrame::NoFrame);
    m_configScrollArea->setWidget(m_configContainer);

    m_content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addLayout(formLayout);
    contentLayout->addWidget(m_divider);
    contentLayout->addWidget(m_configScrollArea, 1);

    bodyLayout()->addWidget(m_content);

    setSelection(false, QString(), QString(), QString(), QJsonObject());

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

void PropertiesPanel::setAvailableDevices(const QVector<DeviceOption>& devices) {
    m_availableDevices = devices;
    if (m_configEditor) {
        m_configEditor->setAvailableDevices(m_availableDevices);
    }
}

void PropertiesPanel::setSelection(bool hasSelection, const QString& typeId, const QString& name,
                                    const QString& key, const QJsonObject& config) {
    m_currentTypeId = typeId;
    m_currentName = name;
    m_currentKey = key;
    m_currentConfig = config;

    m_content->setEnabled(hasSelection);

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

    ensureConfigEditor(hasSelection ? typeId : QString());
    if (m_configEditor) {
        const QSignalBlocker blocker(m_configEditor);
        m_configEditor->setConfig(config);
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

void PropertiesPanel::onConfigEditorChanged() {
    if (!m_configEditor) {
        return;
    }
    const QJsonObject config = m_configEditor->config();
    if (config == m_currentConfig) {
        return;
    }
    emit configChangeRequested(config);
}

void PropertiesPanel::ensureConfigEditor(const QString& typeId) {
    if (typeId == m_configEditorTypeId) {
        return;
    }
    m_configEditorTypeId = typeId;

    if (m_configEditor) {
        m_configLayout->removeWidget(m_configEditor);
        m_configEditor->deleteLater();
        m_configEditor = nullptr;
    }

    std::function<WidgetConfigEditor*(QWidget*)> factory;
    for (const WidgetTypeInfo& info : m_types) {
        if (info.typeId == typeId) {
            factory = info.configEditorFactory;
            break;
        }
    }
    if (!factory) {
        return;
    }

    m_configEditor = factory(nullptr);
    m_configEditor->setAvailableDevices(m_availableDevices);
    m_configLayout->insertWidget(0, m_configEditor);
    connect(m_configEditor, &WidgetConfigEditor::configChanged, this, &PropertiesPanel::onConfigEditorChanged);
}

} // namespace traceview
