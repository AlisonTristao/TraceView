#pragma once

#include "dashboard/widgetconfigeditor.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;

namespace traceview {

class TextBoardConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit TextBoardConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    void emitChanged();
    void updateIdentityDisplay();
    QVector<DeviceOption> devicesWithTextTopics() const;

    bool m_updating = false;
    QVector<DeviceOption> m_devices;
    quint32 m_sourceId = 0;
    quint16 m_topicId = 0;

    QComboBox* m_deviceCombo = nullptr;
    QLineEdit* m_sourceIdEdit = nullptr;
    QComboBox* m_topicIdEdit = nullptr;
    QDoubleSpinBox* m_sampleTimeSpin = nullptr;
    QPlainTextEdit* m_initialTextEdit = nullptr;
};

}  // namespace traceview
