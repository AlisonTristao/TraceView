#include "audiospectrumconfigeditor.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSlider>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace traceview {

namespace {

bool isArrayField(const CatalogTopicField& field) {
    return field.type.contains(QLatin1Char('['));
}

}  // namespace

AudioSpectrumConfigEditor::AudioSpectrumConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});

    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setReadOnly(true);
    m_sourceIdEdit->setPlaceholderText(tr("(auto)"));

    m_topicCombo = new QComboBox(this);
    m_topicCombo->setEditable(true);
    m_topicCombo->lineEdit()->setPlaceholderText(QStringLiteral("0x0101"));
    m_topicCombo->setToolTip(
        tr("Stream topic to play: blocks of samples with their rate and a block counter "
           "(BTPDevice Topics::stream()). Pick a reported topic or type its numeric id."));

    m_fullScaleSpin = new QDoubleSpinBox(this);
    m_fullScaleSpin->setRange(1.0, 1e9);
    m_fullScaleSpin->setDecimals(0);
    m_fullScaleSpin->setValue(2048.0);
    m_fullScaleSpin->setToolTip(
        tr("Sample value that counts as full scale (0 dBFS). Filled from the field's declared "
           "range when a topic is picked."));

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(tr("Device"), m_deviceCombo);
    layout->addRow(tr("Source"), m_sourceIdEdit);
    layout->addRow(tr("Stream topic"), m_topicCombo);
    layout->addRow(tr("Full scale"), m_fullScaleSpin);
    layout->addRow(tr("Volume"), m_volumeSlider);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        populateTopicCombo(m_topicCombo, devicesWithStreamTopics(),
                           m_deviceCombo->currentData().toString());
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_topicCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const qulonglong typed = m_topicCombo->currentText().trimmed().toULongLong(&ok, 0);
        if (ok) {
            m_topicId = quint16(qBound<qulonglong>(0, typed, 65535));
            m_sourceId = 0;
            const QString deviceId = m_deviceCombo->currentData().toString();
            for (const DeviceOption& device : m_devices) {
                if (device.id == deviceId) {
                    m_sourceId = device.selfSourceId;
                    break;
                }
            }
            adoptTopicFields();
        }
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_topicCombo, &QComboBox::activated, this, [this](int index) {
        QString sourceHex;
        QString topicHex;
        if (decodeTopicComboData(m_topicCombo->itemData(index), &sourceHex, &topicHex)) {
            m_sourceId = quint32(sourceHex.toULongLong(nullptr, 0));
            m_topicId = quint16(topicHex.toUInt(nullptr, 0));
            adoptTopicFields();
        }
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_fullScaleSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { emitChanged(); });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int) { emitChanged(); });
}

void AudioSpectrumConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIndex = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIndex >= 0 ? deviceIndex : 0);
    m_sourceId = quint32(config.value("sourceId").toString("0").toULongLong(nullptr, 0));
    m_topicId = quint16(qBound(0, config.value("topicId").toString("0").toInt(nullptr, 0), 65535));
    m_seqFieldId = quint16(config.value("seqFieldId").toInt(1));
    m_rateFieldId = quint16(config.value("rateFieldId").toInt(2));
    m_samplesFieldId = quint16(config.value("samplesFieldId").toInt(3));
    m_fullScaleSpin->setValue(config.value("fullScale").toDouble(2048.0));
    m_volumeSlider->setValue(qBound(0, config.value("volume").toInt(80), 100));
    updateIdentityDisplay();
    m_updating = false;
}

QJsonObject AudioSpectrumConfigEditor::config() const {
    QJsonObject config;
    config["deviceId"] = m_deviceCombo->currentData().toString();
    config["sourceId"] = formatHexId(m_sourceId, 8);
    config["topicId"] = formatHexId(m_topicId, 4);
    config["seqFieldId"] = int(m_seqFieldId);
    config["rateFieldId"] = int(m_rateFieldId);
    config["samplesFieldId"] = int(m_samplesFieldId);
    config["fullScale"] = m_fullScaleSpin->value();
    config["volume"] = m_volumeSlider->value();
    return config;
}

void AudioSpectrumConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_devices = devices;
    populateDeviceCombo(m_deviceCombo, devices);
    populateTopicCombo(m_topicCombo, devicesWithStreamTopics(),
                       m_deviceCombo->currentData().toString());
    updateIdentityDisplay();
    m_updating = wasUpdating;
}

QVector<DeviceOption> AudioSpectrumConfigEditor::devicesWithStreamTopics() const {
    QVector<DeviceOption> filtered = m_devices;
    for (DeviceOption& device : filtered) {
        QVector<CatalogTopicInfo> streams;
        for (const CatalogTopicInfo& topic : device.catalogTopics) {
            if (std::any_of(topic.fields.cbegin(), topic.fields.cend(), isArrayField)) {
                streams.append(topic);
            }
        }
        device.catalogTopics = streams;
    }
    return filtered;
}

void AudioSpectrumConfigEditor::adoptTopicFields() {
    const QVector<CatalogTopicField> fields =
        resolveCatalogTopicFields(m_devices, m_deviceCombo->currentData().toString(),
                                  formatHexId(m_sourceId, 8), formatHexId(m_topicId, 4));
    for (const CatalogTopicField& field : fields) {
        if (isArrayField(field)) {
            m_samplesFieldId = field.fieldId;
            // Full scale: the larger magnitude of the declared range
            // (-2048..2047 -> 2048).
            double scale = 0.0;
            if (!qIsNaN(field.minValue)) {
                scale = std::fabs(field.minValue);
            }
            if (!qIsNaN(field.maxValue)) {
                scale = qMax(scale, std::fabs(field.maxValue) + (field.maxValue > 0 ? 1.0 : 0.0));
            }
            if (scale > 0.0) {
                m_fullScaleSpin->setValue(scale);
            }
        } else if (field.name == QLatin1String("rate")) {
            m_rateFieldId = field.fieldId;
        } else if (field.name == QLatin1String("seq")) {
            m_seqFieldId = field.fieldId;
        }
    }
}

void AudioSpectrumConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

void AudioSpectrumConfigEditor::updateIdentityDisplay() {
    const QString deviceId = m_deviceCombo->currentData().toString();
    const QString topicName = resolveCatalogTopicName(
        devicesWithStreamTopics(), deviceId, formatHexId(m_sourceId, 8), formatHexId(m_topicId, 4));
    m_topicCombo->setCurrentText(topicName.isEmpty() ? formatHexId(m_topicId, 4) : topicName);
    if (m_sourceId == 0) {
        m_sourceIdEdit->clear();
    } else {
        const QString sourceName = resolveSourceLabel(m_devices, m_sourceId);
        m_sourceIdEdit->setText(sourceName.isEmpty() ? formatHexId(m_sourceId, 8) : sourceName);
    }
}

}  // namespace traceview
