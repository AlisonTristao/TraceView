#include "gaugeconfigeditor.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

namespace traceview {

GaugeConfigEditor::GaugeConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setPlaceholderText("0x11223344");
    m_sourceIdEdit->setToolTip("BTP source_id this gauge reads from (hex or decimal).");

    m_topicIdEdit = new QLineEdit(this);
    m_topicIdEdit->setPlaceholderText("0x0101");
    m_topicIdEdit->setToolTip("BTP topic_id (TELEMETRY.md) this gauge's field belongs to.");

    m_fieldIdSpin = new QSpinBox(this);
    m_fieldIdSpin->setRange(0, 65535);
    m_fieldIdSpin->setValue(0);
    m_fieldIdSpin->setToolTip("Which field of the topic this gauge displays.");

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
    m_formLayout->addRow("Source", m_sourceIdEdit);
    m_formLayout->addRow("Topic", m_topicIdEdit);
    m_formLayout->addRow("Field ID", m_fieldIdSpin);
    m_formLayout->addRow("Min", m_minSpin);
    m_formLayout->addRow("Max", m_maxSpin);
    m_formLayout->addRow("Unit", m_unitEdit);
    m_formLayout->addRow("Decimals", m_decimalsSpin);

    connect(m_sourceIdEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_topicIdEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_fieldIdSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_decimalsSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
}

void GaugeConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_sourceIdEdit->setText(config.value("sourceId").toString("0"));
    m_topicIdEdit->setText(config.value("topicId").toString("0"));
    m_fieldIdSpin->setValue(config.value("fieldId").toInt(0));
    m_minSpin->setValue(config.value("min").toDouble(0.0));
    m_maxSpin->setValue(config.value("max").toDouble(100.0));
    m_unitEdit->setText(config.value("unit").toString());
    m_decimalsSpin->setValue(config.value("decimals").toInt(0));
    m_updating = false;
}

QJsonObject GaugeConfigEditor::config() const {
    QJsonObject cfg;
    cfg["sourceId"] = m_sourceIdEdit->text().trimmed().isEmpty() ? QStringLiteral("0") : m_sourceIdEdit->text();
    cfg["topicId"] = m_topicIdEdit->text().trimmed().isEmpty() ? QStringLiteral("0") : m_topicIdEdit->text();
    cfg["fieldId"] = m_fieldIdSpin->value();
    cfg["min"] = m_minSpin->value();
    cfg["max"] = m_maxSpin->value();
    cfg["unit"] = m_unitEdit->text();
    cfg["decimals"] = m_decimalsSpin->value();
    return cfg;
}

void GaugeConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

}  // namespace traceview
