#include "textboardconfigeditor.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>

namespace traceview {

TextBoardConfigEditor::TextBoardConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});

    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setReadOnly(true);
    m_sourceIdEdit->setPlaceholderText(tr("(auto)"));

    m_topicIdEdit = new QComboBox(this);
    m_topicIdEdit->setEditable(true);
    m_topicIdEdit->lineEdit()->setPlaceholderText(tr("0x0003"));
    m_topicIdEdit->setToolTip(
        tr("UTF8 telemetry topic displayed by this board. Pick a reported text topic or type "
           "its numeric id."));

    m_sampleTimeSpin = new QDoubleSpinBox(this);
    m_sampleTimeSpin->setRange(1.0, 3'600'000.0);
    m_sampleTimeSpin->setDecimals(0);
    m_sampleTimeSpin->setSuffix(tr(" ms"));
    m_sampleTimeSpin->setValue(3000.0);
    m_sampleTimeSpin->setToolTip(
        tr("Requested update period. 3000 ms is approximately 0.33 Hz."));

    m_initialTextEdit = new QPlainTextEdit(this);
    m_initialTextEdit->setPlaceholderText(tr("Text shown until the first sample arrives"));
    m_initialTextEdit->setMaximumHeight(96);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(tr("Device"), m_deviceCombo);
    layout->addRow(tr("Source"), m_sourceIdEdit);
    layout->addRow(tr("Text topic"), m_topicIdEdit);
    layout->addRow(tr("Sample period"), m_sampleTimeSpin);
    layout->addRow(tr("Initial text"), m_initialTextEdit);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        populateTopicCombo(m_topicIdEdit, devicesWithTextTopics(),
                           m_deviceCombo->currentData().toString());
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_topicIdEdit->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const qulonglong typed = m_topicIdEdit->currentText().trimmed().toULongLong(&ok, 0);
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
        }
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_topicIdEdit, &QComboBox::activated, this, [this](int index) {
        QString sourceHex;
        QString topicHex;
        if (decodeTopicComboData(m_topicIdEdit->itemData(index), &sourceHex, &topicHex)) {
            m_sourceId = quint32(sourceHex.toULongLong(nullptr, 0));
            m_topicId = quint16(topicHex.toUInt(nullptr, 0));
        }
        updateIdentityDisplay();
        emitChanged();
    });
    connect(m_sampleTimeSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { emitChanged(); });
    connect(m_initialTextEdit, &QPlainTextEdit::textChanged, this,
            &TextBoardConfigEditor::emitChanged);
}

void TextBoardConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIndex = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIndex >= 0 ? deviceIndex : 0);
    m_sourceId = quint32(config.value("sourceId").toString("0").toULongLong(nullptr, 0));
    m_topicId = quint16(qBound(0, config.value("topicId").toString("0").toInt(nullptr, 0),
                              65535));
    m_sampleTimeSpin->setValue(qMax(1.0, config.value("sampleTimeMs").toDouble(3000.0)));
    m_initialTextEdit->setPlainText(config.value("text").toString());
    updateIdentityDisplay();
    m_updating = false;
}

QJsonObject TextBoardConfigEditor::config() const {
    QJsonObject config;
    config["deviceId"] = m_deviceCombo->currentData().toString();
    config["sourceId"] = formatHexId(m_sourceId, 8);
    config["topicId"] = formatHexId(m_topicId, 4);
    config["sampleTimeMs"] = m_sampleTimeSpin->value();
    config["text"] = m_initialTextEdit->toPlainText();
    return config;
}

void TextBoardConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_devices = devices;
    populateDeviceCombo(m_deviceCombo, devices);
    populateTopicCombo(m_topicIdEdit, devicesWithTextTopics(),
                       m_deviceCombo->currentData().toString());
    updateIdentityDisplay();
    m_updating = wasUpdating;
}

QVector<DeviceOption> TextBoardConfigEditor::devicesWithTextTopics() const {
    QVector<DeviceOption> filtered = m_devices;
    for (DeviceOption& device : filtered) {
        QVector<CatalogTopicInfo> textTopics;
        for (const CatalogTopicInfo& topic : device.catalogTopics) {
            if (topic.encoding.compare(QStringLiteral("UTF8"), Qt::CaseInsensitive) == 0) {
                textTopics.append(topic);
            }
        }
        device.catalogTopics = textTopics;
    }
    return filtered;
}

void TextBoardConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

void TextBoardConfigEditor::updateIdentityDisplay() {
    const QString deviceId = m_deviceCombo->currentData().toString();
    const QString topicName = resolveCatalogTopicName(
        devicesWithTextTopics(), deviceId, formatHexId(m_sourceId, 8),
        formatHexId(m_topicId, 4));
    m_topicIdEdit->setCurrentText(topicName.isEmpty() ? formatHexId(m_topicId, 4) : topicName);

    if (m_sourceId == 0) {
        m_sourceIdEdit->clear();
    } else {
        const QString sourceName = resolveSourceLabel(m_devices, m_sourceId);
        m_sourceIdEdit->setText(sourceName.isEmpty() ? formatHexId(m_sourceId, 8) : sourceName);
    }
}

}  // namespace traceview
