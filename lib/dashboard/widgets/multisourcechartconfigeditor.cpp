#include "multisourcechartconfigeditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "dashboard/widgets/multisourcechartwidget.h"

namespace traceview {

namespace {

// A new series' color, by its position.
const char* const kSeriesColors[] = {"#1f77b4", "#d62728", "#2ca02c", "#ff7f0e",
                                     "#9467bd", "#8c564b", "#e377c2", "#17becf"};

struct StyleChoice {
    const char* id;
    const char* label;
};
const StyleChoice kStyles[] = {
    {"solid", QT_TRANSLATE_NOOP("traceview::MultiSourceChartConfigEditor", "Solid")},
    {"dashed", QT_TRANSLATE_NOOP("traceview::MultiSourceChartConfigEditor", "Dashed")},
    {"dotted", QT_TRANSLATE_NOOP("traceview::MultiSourceChartConfigEditor", "Dotted")},
    {"dashdot", QT_TRANSLATE_NOOP("traceview::MultiSourceChartConfigEditor", "Dash-dot")},
};

const DeviceOption* findDevice(const QVector<DeviceOption>& devices, const QString& id) {
    for (const DeviceOption& device : devices) {
        if (device.id == id) {
            return &device;
        }
    }
    return nullptr;
}

}  // namespace

MultiSourceChartConfigEditor::MultiSourceChartConfigEditor(QWidget* parent)
    : WidgetConfigEditor(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* seriesTitle = new QLabel(tr("Series (each from its own device and topic)"), this);
    QFont bold = seriesTitle->font();
    bold.setBold(true);
    seriesTitle->setFont(bold);
    seriesTitle->setWordWrap(true);
    layout->addWidget(seriesTitle);
    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setSpacing(8);
    layout->addLayout(m_rowsLayout);
    auto* add = new QPushButton(tr("+ Add series"), this);
    layout->addWidget(add, 0, Qt::AlignLeft);
    connect(add, &QPushButton::clicked, this, [this] {
        addRow({}, {});
        emitChanged();
    });

    auto* axisTitle = new QLabel(tr("Axes"), this);
    axisTitle->setFont(bold);
    layout->addWidget(axisTitle);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_xLimit = new QSpinBox(this);
    m_xLimit->setRange(1, 1'000'000);
    m_xLimit->setSuffix(tr(" samples"));
    form->addRow(tr("Keep"), m_xLimit);
    m_sampleTime = new QDoubleSpinBox(this);
    m_sampleTime->setRange(0.001, 1'000'000);
    m_sampleTime->setDecimals(3);
    m_sampleTime->setSuffix(tr(" ms"));
    m_sampleTime->setToolTip(tr("The period of one sample: the rate asked of every topic."));
    form->addRow(tr("Sample period"), m_sampleTime);
    m_yMode = new QComboBox(this);
    m_yMode->addItem(tr("Auto"), QStringLiteral("auto"));
    m_yMode->addItem(tr("Fixed"), QStringLiteral("fixed"));
    form->addRow(tr("Y axis"), m_yMode);
    m_yMin = new QDoubleSpinBox(this);
    m_yMax = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* spin : {m_yMin, m_yMax}) {
        spin->setRange(-1e12, 1e12);
        spin->setDecimals(4);
    }
    form->addRow(tr("Min"), m_yMin);
    form->addRow(tr("Max"), m_yMax);
    m_yUnit = new QLineEdit(this);
    m_yUnit->setPlaceholderText(tr("e.g. Speed [m/s]"));
    form->addRow(tr("Unit"), m_yUnit);
    m_decimals = new QSpinBox(this);
    m_decimals->setRange(0, 9);
    form->addRow(tr("Decimals"), m_decimals);
    m_autoAxis = new QCheckBox(tr("One axis per unit"), this);
    form->addRow(QString(), m_autoAxis);
    layout->addLayout(form);

    connect(m_xLimit, qOverload<int>(&QSpinBox::valueChanged), this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(m_sampleTime, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(m_yMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateAxisRows();
        emitChanged();
    });
    connect(m_yMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(m_yMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(m_yUnit, &QLineEdit::editingFinished, this, &MultiSourceChartConfigEditor::emitChanged);
    connect(m_decimals, qOverload<int>(&QSpinBox::valueChanged), this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(m_autoAxis, &QCheckBox::toggled, this, &MultiSourceChartConfigEditor::emitChanged);
    setConfig({});
}

void MultiSourceChartConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_base = config;
    for (const Row& row : std::as_const(m_rows)) {
        delete row.box;
    }
    m_rows.clear();
    const QJsonArray series = config.value(QLatin1String("series")).toArray();
    const QJsonObject bind = config.value(QLatin1String("bind")).toObject();
    for (int i = 0; i < series.size(); ++i) {
        addRow(series[i].toObject(),
               bind.value(MultiSourceLineChartWidget::seriesKey(i)).toObject());
    }
    const QJsonObject xAxis = config.value(QLatin1String("xAxis")).toObject();
    m_xLimit->setValue(xAxis.value(QLatin1String("limit")).toInt(500));
    m_sampleTime->setValue(xAxis.value(QLatin1String("sampleTimeMs")).toDouble(100.0));
    const QJsonObject yAxis = config.value(QLatin1String("yAxis")).toObject();
    m_yMode->setCurrentIndex(yAxis.value(QLatin1String("mode")).toString() == QLatin1String("fixed")
                                 ? 1
                                 : 0);
    m_yMin->setValue(yAxis.value(QLatin1String("min")).toDouble(0.0));
    m_yMax->setValue(yAxis.value(QLatin1String("max")).toDouble(100.0));
    m_yUnit->setText(yAxis.value(QLatin1String("unit")).toString());
    m_decimals->setValue(yAxis.value(QLatin1String("decimals")).toInt(2));
    m_autoAxis->setChecked(yAxis.value(QLatin1String("autoAxis")).toBool(false));
    updateAxisRows();
    m_updating = false;
}

QJsonObject MultiSourceChartConfigEditor::config() const {
    QJsonObject config = m_base;
    QJsonArray series;
    QJsonObject bind;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        QJsonObject entry{{"name", row.name->text()},
                          {"color", row.colorName},
                          {"style", row.style->currentData().toString()}};
        if (!row.unit.isEmpty()) {
            entry.insert(QStringLiteral("unit"), row.unit);
        }
        series.append(entry);
        bind.insert(MultiSourceLineChartWidget::seriesKey(i), row.binding);
    }
    config.insert(QStringLiteral("series"), series);
    config.insert(QStringLiteral("bind"), bind);
    QJsonObject xAxis = m_base.value(QLatin1String("xAxis")).toObject();
    xAxis.insert(QStringLiteral("mode"), QStringLiteral("samples"));
    xAxis.insert(QStringLiteral("limit"), m_xLimit->value());
    xAxis.insert(QStringLiteral("sampleTimeMs"), m_sampleTime->value());
    config.insert(QStringLiteral("xAxis"), xAxis);
    QJsonObject yAxis = m_base.value(QLatin1String("yAxis")).toObject();
    yAxis.insert(QStringLiteral("mode"), m_yMode->currentData().toString());
    yAxis.insert(QStringLiteral("min"), m_yMin->value());
    yAxis.insert(QStringLiteral("max"), m_yMax->value());
    yAxis.insert(QStringLiteral("unit"), m_yUnit->text());
    yAxis.insert(QStringLiteral("decimals"), m_decimals->value());
    yAxis.insert(QStringLiteral("autoAxis"), m_autoAxis->isChecked());
    if (!yAxis.contains(QLatin1String("grid"))) {
        yAxis.insert(QStringLiteral("grid"), true);
    }
    config.insert(QStringLiteral("yAxis"), yAxis);
    return config;
}

void MultiSourceChartConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    m_devices = devices;
    const bool updating = m_updating;
    m_updating = true;
    for (Row& row : m_rows) {
        populateDeviceCombo(row.device, m_devices);
        const int index = row.device->findData(row.binding.value(QLatin1String("deviceId")).toString());
        row.device->setCurrentIndex(qMax(0, index));
        fillTopics(row);
    }
    m_updating = updating;
}

void MultiSourceChartConfigEditor::addRow(const QJsonObject& series, const QJsonObject& binding) {
    Row row;
    row.binding = binding;
    row.unit = series.value(QLatin1String("unit")).toString();
    row.box = new QFrame(this);
    static_cast<QFrame*>(row.box)->setFrameShape(QFrame::StyledPanel);
    auto* form = new QFormLayout(row.box);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(8, 8, 8, 8);
    row.device = new QComboBox(row.box);
    form->addRow(tr("Device"), row.device);
    row.topic = new QComboBox(row.box);
    form->addRow(tr("Topic"), row.topic);
    row.field = new QComboBox(row.box);
    form->addRow(tr("Field"), row.field);
    row.name = new QLineEdit(series.value(QLatin1String("name")).toString(), row.box);
    form->addRow(tr("Name"), row.name);
    auto* looks = new QHBoxLayout;
    row.style = new QComboBox(row.box);
    for (const StyleChoice& style : kStyles) {
        row.style->addItem(tr(style.label), QString::fromLatin1(style.id));
    }
    row.style->setCurrentIndex(
        qMax(0, row.style->findData(series.value(QLatin1String("style")).toString("solid"))));
    looks->addWidget(row.style, 1);
    row.color = new QPushButton(row.box);
    row.color->setFixedWidth(40);
    row.color->setToolTip(tr("The series' color"));
    looks->addWidget(row.color);
    auto* remove = new QPushButton(tr("Remove"), row.box);
    looks->addWidget(remove);
    form->addRow(tr("Line"), looks);
    m_rowsLayout->addWidget(row.box);
    m_rows.append(row);
    Row& added = m_rows.last();
    setColor(added, series.value(QLatin1String("color"))
                        .toString(QString::fromLatin1(
                            kSeriesColors[(m_rows.size() - 1) % std::size(kSeriesColors)])));

    populateDeviceCombo(added.device, m_devices);
    added.device->setCurrentIndex(
        qMax(0, added.device->findData(binding.value(QLatin1String("deviceId")).toString())));
    fillTopics(added);

    QWidget* box = added.box;
    const auto rowOf = [this, box]() -> Row* {
        for (Row& r : m_rows) {
            if (r.box == box) {
                return &r;
            }
        }
        return nullptr;
    };
    connect(added.device, qOverload<int>(&QComboBox::activated), this, [this, rowOf] {
        Row* r = rowOf();
        r->binding = QJsonObject{{"deviceId", r->device->currentData().toString()}};
        fillTopics(*r);
        emitChanged();
    });
    connect(added.topic, qOverload<int>(&QComboBox::activated), this, [this, rowOf] {
        Row* r = rowOf();
        const QVariantList topic = r->topic->currentData().toList();
        r->binding = QJsonObject{{"deviceId", r->device->currentData().toString()},
                                 {"sourceId", formatHexId(topic.value(0).toUInt(), 8)},
                                 {"topicId", formatHexId(topic.value(1).toUInt(), 4)},
                                 {"topicName", topic.value(2).toString()},
                                 {"fieldId", 0}};
        fillFields(*r);
        emitChanged();
    });
    connect(added.field, qOverload<int>(&QComboBox::activated), this, [this, rowOf] {
        Row* r = rowOf();
        const QVariantList field = r->field->currentData().toList();
        const QString previous = r->binding.value(QLatin1String("fieldName")).toString();
        r->binding.insert(QStringLiteral("fieldId"), field.value(0).toInt());
        r->binding.insert(QStringLiteral("fieldName"), field.value(1).toString());
        r->unit = field.value(2).toString();
        // A name still the field's (or none) follows the field.
        if (r->name->text().isEmpty() || r->name->text() == previous) {
            r->name->setText(field.value(1).toString());
        }
        emitChanged();
    });
    connect(added.name, &QLineEdit::editingFinished, this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(added.style, qOverload<int>(&QComboBox::activated), this,
            &MultiSourceChartConfigEditor::emitChanged);
    connect(added.color, &QPushButton::clicked, this, [this, rowOf] {
        Row* r = rowOf();
        const QColor picked = QColorDialog::getColor(QColor(r->colorName), this);
        if (picked.isValid()) {
            setColor(*r, picked.name());
            emitChanged();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, box] {
        removeRow(box);
        emitChanged();
    });
}

void MultiSourceChartConfigEditor::removeRow(QWidget* box) {
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].box == box) {
            m_rows.removeAt(i);
            box->deleteLater();
            return;
        }
    }
}

void MultiSourceChartConfigEditor::fillTopics(Row& row) {
    row.topic->clear();
    const DeviceOption* device = findDevice(m_devices, row.device->currentData().toString());
    const QString boundName = row.binding.value(QLatin1String("topicName")).toString();
    const quint32 boundSource =
        quint32(row.binding.value(QLatin1String("sourceId")).toString("0").toULongLong(nullptr, 0));
    const quint16 boundTopic = quint16(
        row.binding.value(QLatin1String("topicId")).toString("0").toInt(nullptr, 0));
    int current = -1;
    if (device) {
        for (const CatalogTopicInfo& topic : device->catalogTopics) {
            bool plottable = false;
            for (const CatalogTopicField& field : topic.fields) {
                plottable = plottable || isPlottableScalarField(field);
            }
            if (!plottable) {
                continue;
            }
            row.topic->addItem(topic.name.isEmpty() ? formatHexId(topic.topicId, 4) : topic.name,
                               QVariantList{topic.sourceId, topic.topicId, topic.name});
            if ((!boundName.isEmpty() && topic.name == boundName) ||
                (boundName.isEmpty() && topic.sourceId == boundSource &&
                 topic.topicId == boundTopic && boundTopic != 0)) {
                current = row.topic->count() - 1;
            }
        }
    }
    if (current < 0 && !boundName.isEmpty()) {
        // Not in the catalog now (the device offline): what it was bound to.
        row.topic->addItem(boundName, QVariantList{boundSource, boundTopic, boundName});
        current = row.topic->count() - 1;
    }
    row.topic->setCurrentIndex(current);
    fillFields(row);
}

void MultiSourceChartConfigEditor::fillFields(Row& row) {
    row.field->clear();
    const QVariantList topic = row.topic->currentData().toList();
    const DeviceOption* device = findDevice(m_devices, row.device->currentData().toString());
    const QString boundName = row.binding.value(QLatin1String("fieldName")).toString();
    const int boundId = row.binding.value(QLatin1String("fieldId")).toInt(0);
    int current = -1;
    if (device && !topic.isEmpty()) {
        for (const CatalogTopicInfo& info : device->catalogTopics) {
            if (info.sourceId != topic.value(0).toUInt() || info.topicId != topic.value(1).toUInt()) {
                continue;
            }
            for (const CatalogTopicField& field : info.fields) {
                if (!isPlottableScalarField(field)) {
                    continue;
                }
                const QString label = field.unit.isEmpty()
                                          ? field.name
                                          : QStringLiteral("%1 [%2]").arg(field.name, field.unit);
                row.field->addItem(label, QVariantList{field.fieldId, field.name, field.unit});
                if ((!boundName.isEmpty() && field.name == boundName) ||
                    (boundName.isEmpty() && boundId != 0 && field.fieldId == boundId)) {
                    current = row.field->count() - 1;
                }
            }
        }
    }
    if (current < 0 && !boundName.isEmpty()) {
        row.field->addItem(boundName, QVariantList{boundId, boundName, row.unit});
        current = row.field->count() - 1;
    }
    row.field->setCurrentIndex(current);
}

void MultiSourceChartConfigEditor::setColor(Row& row, const QString& color) {
    row.colorName = QColor(color).isValid() ? QColor(color).name() : QStringLiteral("#3B82F6");
    row.color->setStyleSheet(QStringLiteral("background-color: %1;").arg(row.colorName));
}

void MultiSourceChartConfigEditor::updateAxisRows() {
    const bool fixed = m_yMode->currentIndex() == 1;
    m_yMin->setEnabled(fixed);
    m_yMax->setEnabled(fixed);
}

void MultiSourceChartConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

}  // namespace traceview
