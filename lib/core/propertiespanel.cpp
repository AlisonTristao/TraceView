#include "propertiespanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "dashboard/widgetregistry.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "traceview/thememanager.h"

namespace traceview {

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(kPropertiesPanelWidth);

    m_pinButton = new QToolButton(this);
    m_pinButton->setObjectName("pinButton");
    m_pinButton->setCheckable(true);
    m_pinButton->setAutoRaise(true);
    m_pinButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_pinButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    connect(m_pinButton, &QToolButton::toggled, this, &PropertiesPanel::onPinToggled);

    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);
    topBar->addStretch(1);
    topBar->addWidget(m_pinButton);

    m_typeCombo = new QComboBox();
    m_nameEdit = new QLineEdit();
    m_keyEdit = new QLineEdit();
    m_keyEdit->setPlaceholderText("(none)");
    m_keyEdit->setToolTip("Optional, must be unique — the handle future data updates will target this widget by.");

    auto* formLayout = new QFormLayout();
    formLayout->addRow("Type", m_typeCombo);
    formLayout->addRow("Name", m_nameEdit);
    formLayout->addRow("Key", m_keyEdit);

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

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topBar);
    mainLayout->addWidget(m_content, 1);

    setSelection(false, QString(), QString(), QString(), QJsonObject());
    updatePinIcon();

    connect(m_typeCombo, &QComboBox::activated, this, &PropertiesPanel::onTypeActivated);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &PropertiesPanel::onNameEditingFinished);
    connect(m_keyEdit, &QLineEdit::editingFinished, this, &PropertiesPanel::onKeyEditingFinished);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updatePinIcon(); });
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

void PropertiesPanel::onPinToggled(bool checked) {
    m_pinned = checked;
    updatePinIcon();
    emit pinnedChanged(checked);
}

void PropertiesPanel::updatePinIcon() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_pinButton->setIcon(makePinIcon(m_pinned ? palette.accent : palette.textPrimary, m_pinned));
    m_pinButton->setToolTip(m_pinned ? "Unpin — panel will hide when nothing is selected"
                                      : "Pin — keep panel open with nothing selected");
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
    m_configLayout->insertWidget(0, m_configEditor);
    connect(m_configEditor, &WidgetConfigEditor::configChanged, this, &PropertiesPanel::onConfigEditorChanged);
}

} // namespace traceview
