#include "gaugeconfigeditor.h"

#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kNameColumn = 0;
constexpr int kFieldIdColumn = 1;
constexpr int kColorColumn = 2;
constexpr int kRemoveColumn = 3;
constexpr int kColumnCount = 4;

QColor swatchColor(QPushButton* button) { return button->property("swatchColor").value<QColor>(); }

void setSwatchColor(QPushButton* button, const QColor& color) {
    button->setProperty("swatchColor", color);
    button->setStyleSheet(QString("background-color: %1;").arg(color.name()));
}

}  // namespace

GaugeConfigEditor::GaugeConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(tr("Which device this gauge reads from -- must match the sourceId below."));

    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setReadOnly(true);
    m_sourceIdEdit->setPlaceholderText(tr("(auto)"));
    m_sourceIdEdit->setToolTip(
        tr("BTP source_id this gauge reads from -- derived from the Topic field below, shown by device name "
           "when known."));

    m_topicIdEdit = new QComboBox(this);
    m_topicIdEdit->setEditable(true);
    populateTopicCombo(m_topicIdEdit, {}, QString());
    m_topicIdEdit->lineEdit()->setPlaceholderText(tr("0x0101"));
    m_topicIdEdit->setToolTip(tr("BTP topic_id (TELEMETRY.md) this gauge's rings bind fields of -- pick one the "
                                  "device has already reported (shown by name), or type a hex/decimal id by hand "
                                  "for one it hasn't reported yet."));

    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-100'000.0, 100'000.0);
    m_minSpin->setDecimals(2);
    m_minSpin->setValue(0.0);
    m_minSpin->setToolTip(tr("Shared scale floor -- every ring maps its own field onto this same range."));

    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-100'000.0, 100'000.0);
    m_maxSpin->setDecimals(2);
    m_maxSpin->setValue(100.0);
    m_maxSpin->setToolTip(tr("Shared scale ceiling -- every ring maps its own field onto this same range."));

    m_unitEdit = new QLineEdit(this);
    m_unitEdit->setPlaceholderText(tr("V, °C, %..."));

    m_decimalsSpin = new QSpinBox(this);
    m_decimalsSpin->setRange(0, 6);
    m_decimalsSpin->setValue(0);
    m_decimalsSpin->setToolTip(tr("Decimal places shown for each ring's current value."));

    m_formLayout = new QFormLayout();
    m_formLayout->setContentsMargins(0, 0, 0, 0);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(tr("Device"), m_deviceCombo);
    m_formLayout->addRow(tr("Source"), m_sourceIdEdit);
    m_formLayout->addRow(tr("Topic"), m_topicIdEdit);
    m_formLayout->addRow(tr("Min"), m_minSpin);
    m_formLayout->addRow(tr("Max"), m_maxSpin);
    m_formLayout->addRow(tr("Unit"), m_unitEdit);
    m_formLayout->addRow(tr("Decimals"), m_decimalsSpin);

    auto* divider = new QFrame(this);
    divider->setObjectName("sectionDivider");
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);

    // One row per concentric ring the gauge draws, outermost first (row 0 =
    // outermost ring) -- see DummyGaugeWidget::paintEvent().
    m_seriesTable = new QTableWidget(0, kColumnCount, this);
    m_seriesTable->setHorizontalHeaderLabels({tr("Name"), tr("Field ID"), tr("Color"), ""});
    m_seriesTable->verticalHeader()->setVisible(false);
    m_seriesTable->horizontalHeader()->setSectionResizeMode(kRemoveColumn, QHeaderView::ResizeToContents);
    m_seriesTable->setColumnWidth(kNameColumn, 150);
    m_seriesTable->setColumnWidth(kFieldIdColumn, 90);
    m_seriesTable->setColumnWidth(kColorColumn, 56);
    m_seriesTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_seriesTable->verticalHeader()->setDefaultSectionSize(36);
    m_seriesTable->setMinimumHeight(120);

    m_addSeriesButton = new QPushButton(tr("+ Add ring"), this);
    connect(m_addSeriesButton, &QPushButton::clicked, this, [this]() {
        QJsonObject series;
        series["fieldId"] = m_seriesTable->rowCount() + 1;
        addSeriesRow(series);
        emitChanged();
    });

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_addSeriesButton);
    buttonRow->addStretch(1);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 8, 0, 0);
    mainLayout->addLayout(m_formLayout);
    mainLayout->addWidget(divider);
    mainLayout->addWidget(m_seriesTable);
    mainLayout->addLayout(buttonRow);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        populateTopicCombo(m_topicIdEdit, m_devices, m_deviceCombo->currentData().toString());
        updateIdentityDisplay();
        emitChanged();
    });
    // Manual entry: only a plain hex/decimal id is accepted (the field also
    // shows resolved names, which aren't meant to be typed back in). A
    // hand-typed topic id is assumed to belong to the selected device's own
    // identity -- see DeviceOption::selfSourceId -- since each robot is its
    // own Device now and Source has no separate manual-entry path any more.
    // Unparseable text (most commonly the resolved name still sitting there,
    // untouched) is ignored and the display simply reverts to the last valid
    // binding.
    connect(m_topicIdEdit->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const qulonglong typed = m_topicIdEdit->currentText().trimmed().toULongLong(&ok, 0);
        if (ok) {
            m_topicId = quint16(qBound<qulonglong>(0, typed, 65535));
            m_sourceId = 0;
            if (m_topicId != 0) {
                const QString deviceId = m_deviceCombo->currentData().toString();
                for (const DeviceOption& device : m_devices) {
                    if (device.id == deviceId) {
                        m_sourceId = device.selfSourceId;
                        break;
                    }
                }
            }
        }
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_topicIdEdit, &QComboBox::activated, this, [this](int index) {
        QString sourceHex, topicHex;
        if (decodeTopicComboData(m_topicIdEdit->itemData(index), &sourceHex, &topicHex)) {
            m_sourceId = quint32(sourceHex.toULongLong(nullptr, 0));
            m_topicId = quint16(topicHex.toUInt(nullptr, 0));
        }
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_decimalsSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_seriesTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { emitChanged(); });
}

void GaugeConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    m_sourceId = quint32(config.value("sourceId").toString("0").toULongLong(nullptr, 0));
    m_topicId = quint16(qBound(0, config.value("topicId").toString("0").toInt(nullptr, 0), 65535));
    m_minSpin->setValue(config.value("min").toDouble(0.0));
    m_maxSpin->setValue(config.value("max").toDouble(100.0));
    m_unitEdit->setText(config.value("unit").toString());
    m_decimalsSpin->setValue(config.value("decimals").toInt(0));

    m_seriesTable->setRowCount(0);
    if (config.contains("series")) {
        for (const QJsonValue& value : config.value("series").toArray()) {
            addSeriesRow(value.toObject());
        }
    } else if (config.contains("fieldId")) {
        // Pre-multi-ring save: migrate the bare top-level fieldId into a
        // single row, same fallback parseGaugeConfig() (chartdata.cpp)
        // applies when actually rendering the gauge.
        QJsonObject legacy;
        legacy["fieldId"] = config.value("fieldId").toInt(0);
        addSeriesRow(legacy);
    } else {
        // Brand-new widget, never configured -- start with one ring instead
        // of an empty table, so the properties panel doesn't open on a
        // gauge that looks unconfigurable.
        addSeriesRow(QJsonObject());
    }

    updateIdentityDisplay();
    m_updating = false;
}

QJsonObject GaugeConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    cfg["sourceId"] = formatHexId(m_sourceId, 8);
    cfg["topicId"] = formatHexId(m_topicId, 4);
    cfg["min"] = m_minSpin->value();
    cfg["max"] = m_maxSpin->value();
    cfg["unit"] = m_unitEdit->text();
    cfg["decimals"] = m_decimalsSpin->value();

    QJsonArray seriesArray;
    for (int row = 0; row < m_seriesTable->rowCount(); ++row) {
        QJsonObject series;
        const QTableWidgetItem* nameItem = m_seriesTable->item(row, kNameColumn);
        series["name"] = nameItem ? nameItem->text() : QString();

        if (auto* fieldIdSpin = qobject_cast<QSpinBox*>(m_seriesTable->cellWidget(row, kFieldIdColumn))) {
            series["fieldId"] = fieldIdSpin->value();
        }
        if (auto* colorButton = qobject_cast<QPushButton*>(m_seriesTable->cellWidget(row, kColorColumn))) {
            series["color"] = swatchColor(colorButton).name();
        }
        seriesArray.append(series);
    }
    cfg["series"] = seriesArray;
    return cfg;
}

void GaugeConfigEditor::addSeriesRow(const QJsonObject& series) {
    const bool wasUpdating = m_updating;
    m_updating = true;

    const int row = m_seriesTable->rowCount();
    m_seriesTable->insertRow(row);

    auto* nameItem = new QTableWidgetItem(series.value("name").toString(tr("Ring %1").arg(row + 1)));
    m_seriesTable->setItem(row, kNameColumn, nameItem);

    auto* fieldIdSpin = new QSpinBox();
    fieldIdSpin->setRange(0, 65535);
    fieldIdSpin->setValue(series.value("fieldId").toInt(row + 1));
    fieldIdSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(fieldIdSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    m_seriesTable->setCellWidget(row, kFieldIdColumn, fieldIdSpin);

    auto* colorButton = new QPushButton();
    colorButton->setFixedWidth(40);
    // No "color" key means this is a brand-new row (Add ring button, or an
    // old save predating this field) -- default it to the theme's series
    // palette instead of a hardcoded blue so a new ring's color already
    // fits the active theme and differs from the previous row. Existing rows
    // with an explicit color are untouched.
    const QVector<QColor>& seriesPalette = ThemeManager::instance().currentTheme().series;
    const QColor fallback = seriesPalette.isEmpty() ? QColor("#3B82F6") : seriesPalette[row % seriesPalette.size()];
    setSwatchColor(colorButton, QColor(series.value("color").toString(fallback.name())));
    connect(colorButton, &QPushButton::clicked, this, [this, colorButton]() {
        const QColor chosen = QColorDialog::getColor(swatchColor(colorButton), this, tr("Ring Color"));
        if (!chosen.isValid()) {
            return;
        }
        setSwatchColor(colorButton, chosen);
        emitChanged();
    });
    m_seriesTable->setCellWidget(row, kColorColumn, colorButton);

    auto* removeButton = new QPushButton("✕");
    removeButton->setFixedWidth(28);
    removeButton->setToolTip(tr("Remove ring"));
    connect(removeButton, &QPushButton::clicked, this, [this, removeButton]() {
        for (int r = 0; r < m_seriesTable->rowCount(); ++r) {
            if (m_seriesTable->cellWidget(r, kRemoveColumn) == removeButton) {
                m_seriesTable->removeRow(r);
                break;
            }
        }
        emitChanged();
    });
    m_seriesTable->setCellWidget(row, kRemoveColumn, removeButton);

    m_updating = wasUpdating;
}

void GaugeConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_devices = devices;
    populateDeviceCombo(m_deviceCombo, devices);
    populateTopicCombo(m_topicIdEdit, devices, m_deviceCombo->currentData().toString());
    updateIdentityDisplay();
    m_updating = wasUpdating;
}

void GaugeConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

void GaugeConfigEditor::updateIdentityDisplay() {
    const QString deviceId = m_deviceCombo->currentData().toString();
    const QString topicName =
        resolveCatalogTopicName(m_devices, deviceId, formatHexId(m_sourceId, 8), formatHexId(m_topicId, 4));
    m_topicIdEdit->setCurrentText(topicName.isEmpty() ? formatHexId(m_topicId, 4) : topicName);

    if (m_sourceId == 0) {
        m_sourceIdEdit->clear();
    } else {
        const QString sourceName = resolveSourceLabel(m_devices, m_sourceId);
        m_sourceIdEdit->setText(sourceName.isEmpty() ? formatHexId(m_sourceId, 8) : sourceName);
    }
}

}  // namespace traceview
