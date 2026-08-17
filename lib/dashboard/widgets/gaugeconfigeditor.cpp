#include "gaugeconfigeditor.h"

#include <QColor>
#include <QColorDialog>
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
    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setPlaceholderText(tr("0x11223344"));
    m_sourceIdEdit->setToolTip(tr("BTP source_id this gauge reads from (hex or decimal)."));

    m_topicIdEdit = new QLineEdit(this);
    m_topicIdEdit->setPlaceholderText(tr("0x0101"));
    m_topicIdEdit->setToolTip(tr("BTP topic_id (TELEMETRY.md) this gauge's rings bind fields of."));

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

    connect(m_sourceIdEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_topicIdEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_decimalsSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_seriesTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { emitChanged(); });
}

void GaugeConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_sourceIdEdit->setText(config.value("sourceId").toString("0"));
    m_topicIdEdit->setText(config.value("topicId").toString("0"));
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

    m_updating = false;
}

QJsonObject GaugeConfigEditor::config() const {
    QJsonObject cfg;
    cfg["sourceId"] = m_sourceIdEdit->text().trimmed().isEmpty() ? QStringLiteral("0") : m_sourceIdEdit->text();
    cfg["topicId"] = m_topicIdEdit->text().trimmed().isEmpty() ? QStringLiteral("0") : m_topicIdEdit->text();
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

void GaugeConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

}  // namespace traceview
