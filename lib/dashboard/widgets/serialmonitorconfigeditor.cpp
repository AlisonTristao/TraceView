#include "serialmonitorconfigeditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace traceview {

SerialMonitorConfigEditor::SerialMonitorConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 8, 0, 0);

    auto* heading = new QLabel(tr("Tabs — one terminal per device"), this);
    heading->setWordWrap(true);
    outerLayout->addWidget(heading);

    m_rowsLayout = new QVBoxLayout();
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(4);
    outerLayout->addLayout(m_rowsLayout);

    auto* addButton = new QPushButton(tr("Add tab"), this);
    addButton->setToolTip(tr("Add another terminal tab for a device."));
    connect(addButton, &QPushButton::clicked, this, [this] {
        addRowWidget(QString());
        onStructureChanged();
    });
    outerLayout->addWidget(addButton, 0, Qt::AlignLeft);

    // Start with one row so a freshly inserted widget round-trips to a valid
    // single-tab config even before the user touches anything. Guarded: the
    // combo's initial currentIndexChanged must not be mistaken for a user edit.
    m_updating = true;
    addRowWidget(QString());
    m_updating = false;
}

void SerialMonitorConfigEditor::setConfig(const QJsonObject& config) {
    QStringList deviceIds;
    if (config.value("tabs").isArray()) {
        const QJsonArray tabs = config.value("tabs").toArray();
        for (const QJsonValue& tab : tabs) {
            deviceIds.append(tab.toObject().value("deviceId").toString());
        }
    } else if (config.contains("deviceId")) {
        deviceIds.append(config.value("deviceId").toString());
    }
    if (deviceIds.isEmpty()) {
        deviceIds.append(QString());
    }

    m_updating = true;
    rebuildRows(deviceIds);
    m_updating = false;
}

QJsonObject SerialMonitorConfigEditor::config() const {
    QJsonArray tabs;
    for (QComboBox* combo : m_deviceCombos) {
        QJsonObject tab;
        tab["deviceId"] = combo->currentData().toString();
        tabs.append(tab);
    }
    QJsonObject cfg;
    cfg["tabs"] = tabs;
    return cfg;
}

void SerialMonitorConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    m_devices = devices;
    const bool wasUpdating = m_updating;
    m_updating = true;
    for (QComboBox* combo : m_deviceCombos) {
        populateDeviceCombo(combo, devices);
    }
    m_updating = wasUpdating;
}

QStringList SerialMonitorConfigEditor::currentDeviceIds() const {
    QStringList ids;
    for (QComboBox* combo : m_deviceCombos) {
        ids.append(combo->currentData().toString());
    }
    return ids;
}

void SerialMonitorConfigEditor::rebuildRows(const QStringList& deviceIds) {
    while (!m_deviceCombos.isEmpty()) {
        // Each combo's whole row widget is its grandparent (row -> hbox is not
        // a widget; the row container is the combo's parentWidget()).
        QWidget* rowWidget = m_deviceCombos.takeLast()->parentWidget();
        delete rowWidget;
    }
    for (const QString& deviceId : deviceIds) {
        addRowWidget(deviceId);
    }
}

void SerialMonitorConfigEditor::addRowWidget(const QString& deviceId) {
    auto* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(4);

    auto* combo = new QComboBox(row);
    populateDeviceCombo(combo, m_devices);
    const int idx = combo->findData(deviceId);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    combo->setToolTip(tr("Device this tab's terminal talks to."));
    connect(combo, &QComboBox::currentIndexChanged, this, [this](int) { onPickChanged(); });
    rowLayout->addWidget(combo, 1);

    auto* upButton = new QToolButton(row);
    upButton->setText(QStringLiteral("▲"));
    upButton->setToolTip(tr("Move tab left"));
    connect(upButton, &QToolButton::clicked, this, [this, combo] {
        const int i = m_deviceCombos.indexOf(combo);
        if (i > 0) {
            QStringList ids = currentDeviceIds();
            ids.move(i, i - 1);
            m_updating = true;
            rebuildRows(ids);
            m_updating = false;
            onStructureChanged();
        }
    });
    rowLayout->addWidget(upButton);

    auto* downButton = new QToolButton(row);
    downButton->setText(QStringLiteral("▼"));
    downButton->setToolTip(tr("Move tab right"));
    connect(downButton, &QToolButton::clicked, this, [this, combo] {
        const int i = m_deviceCombos.indexOf(combo);
        if (i >= 0 && i < m_deviceCombos.size() - 1) {
            QStringList ids = currentDeviceIds();
            ids.move(i, i + 1);
            m_updating = true;
            rebuildRows(ids);
            m_updating = false;
            onStructureChanged();
        }
    });
    rowLayout->addWidget(downButton);

    auto* removeButton = new QToolButton(row);
    removeButton->setText(QStringLiteral("✕"));
    removeButton->setToolTip(tr("Remove this tab"));
    connect(removeButton, &QToolButton::clicked, this, [this, combo] {
        if (m_deviceCombos.size() <= 1) {
            return;  // keep at least one tab
        }
        QWidget* rowWidget = combo->parentWidget();
        m_deviceCombos.removeAll(combo);
        delete rowWidget;
        onStructureChanged();
    });
    rowLayout->addWidget(removeButton);

    m_rowsLayout->addWidget(row);
    m_deviceCombos.append(combo);
}

void SerialMonitorConfigEditor::onStructureChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

void SerialMonitorConfigEditor::onPickChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

}  // namespace traceview
