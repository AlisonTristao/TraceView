#pragma once

#include "dashboard/widgetconfigeditor.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSlider;

namespace traceview {

// Properties of an Audio Analyzer widget (AudioAnalyzerWidget): the device,
// the stream topic (any topic with an array field), the samples' full scale
// and the volume. Picking a topic fills the seq/rate/samples field ids and
// the full scale from the device's catalogue.
class AudioSpectrumConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit AudioSpectrumConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    void emitChanged();
    void updateIdentityDisplay();
    void adoptTopicFields();
    QVector<DeviceOption> devicesWithStreamTopics() const;

    bool m_updating = false;
    QVector<DeviceOption> m_devices;
    quint32 m_sourceId = 0;
    quint16 m_topicId = 0;
    quint16 m_seqFieldId = 1;
    quint16 m_rateFieldId = 2;
    quint16 m_samplesFieldId = 3;

    QComboBox* m_deviceCombo = nullptr;
    QLineEdit* m_sourceIdEdit = nullptr;
    QComboBox* m_topicCombo = nullptr;
    QDoubleSpinBox* m_fullScaleSpin = nullptr;
    QSlider* m_volumeSlider = nullptr;
};

}  // namespace traceview
