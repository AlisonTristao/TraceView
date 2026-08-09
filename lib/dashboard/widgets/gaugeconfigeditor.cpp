#include "gaugeconfigeditor.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

namespace traceview {

namespace {
// Same ids/convention as ChartConfigEditor's kByteTypeIds — kept as its own
// copy rather than a shared header since the two editors don't otherwise
// depend on each other and this is the only field they'd share.
const QStringList kByteTypeIds = {"uint8", "int8", "uint16", "int16", "uint32", "int32", "float32", "float64"};
const QStringList kByteTypeLabels = {"UInt8", "Int8", "UInt16", "Int16", "UInt32", "Int32", "Float32", "Float64"};
} // namespace

GaugeConfigEditor::GaugeConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem("CSV", "csv");
    m_formatCombo->addItem("Bytes", "bytes");
    m_formatCombo->setToolTip("How the incoming data frame is shaped.");

    m_indexSpin = new QSpinBox(this);
    m_indexSpin->setRange(0, 9999);
    m_indexSpin->setValue(0);
    m_indexSpin->setToolTip("Which slot of the incoming frame this gauge reads.");

    m_byteTypeCombo = new QComboBox(this);
    m_byteTypeCombo->addItems(kByteTypeLabels);

    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-100'000.0, 100'000.0);
    m_minSpin->setDecimals(2);
    m_minSpin->setValue(0.0);

    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-100'000.0, 100'000.0);
    m_maxSpin->setDecimals(2);
    m_maxSpin->setValue(100.0);

    m_unitEdit = new QLineEdit(this);
    m_unitEdit->setPlaceholderText("V, °C, %...");

    m_decimalsSpin = new QSpinBox(this);
    m_decimalsSpin->setRange(0, 6);
    m_decimalsSpin->setValue(0);
    m_decimalsSpin->setToolTip("Decimal places shown for the current value.");

    m_formLayout = new QFormLayout(this);
    m_formLayout->setContentsMargins(0, 8, 0, 0);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow("Format", m_formatCombo);
    m_formLayout->addRow("Index", m_indexSpin);
    m_formLayout->addRow("Type", m_byteTypeCombo);
    m_formLayout->addRow("Min", m_minSpin);
    m_formLayout->addRow("Max", m_maxSpin);
    m_formLayout->addRow("Unit", m_unitEdit);
    m_formLayout->addRow("Decimals", m_decimalsSpin);

    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateByteTypeVisibility();
        emitChanged();
    });
    connect(m_indexSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_byteTypeCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_decimalsSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });

    updateByteTypeVisibility();
}

void GaugeConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_formatCombo->setCurrentIndex(config.value("format").toString("csv") == "bytes" ? 1 : 0);
    m_indexSpin->setValue(config.value("index").toInt(0));
    m_byteTypeCombo->setCurrentIndex(qMax(0, kByteTypeIds.indexOf(config.value("byteType").toString("float32"))));
    m_minSpin->setValue(config.value("min").toDouble(0.0));
    m_maxSpin->setValue(config.value("max").toDouble(100.0));
    m_unitEdit->setText(config.value("unit").toString());
    m_decimalsSpin->setValue(config.value("decimals").toInt(0));
    updateByteTypeVisibility();
    m_updating = false;
}

QJsonObject GaugeConfigEditor::config() const {
    QJsonObject cfg;
    cfg["format"] = m_formatCombo->currentData().toString();
    cfg["index"] = m_indexSpin->value();
    cfg["byteType"] = kByteTypeIds.value(m_byteTypeCombo->currentIndex(), kByteTypeIds.first());
    cfg["min"] = m_minSpin->value();
    cfg["max"] = m_maxSpin->value();
    cfg["unit"] = m_unitEdit->text();
    cfg["decimals"] = m_decimalsSpin->value();
    return cfg;
}

void GaugeConfigEditor::updateByteTypeVisibility() {
    m_formLayout->setRowVisible(m_byteTypeCombo, m_formatCombo->currentData().toString() == "bytes");
}

void GaugeConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

} // namespace traceview
