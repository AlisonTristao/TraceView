#pragma once

#include <QJsonValue>

#include "dashboard/widgetconfigeditor.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace traceview {

// Properties-panel section for ChatWidget: the device the chat runs through
// (none keeps it visual only), the device's event-log text topic (by name,
// default "chat"), the command a message is sent with (default
// "chat -send"), the name outgoing messages carry and whether they are drawn
// mirrored on the right, messenger-style.
class ChatConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit ChatConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    void populateTopics();
    // The device or topic name changed: the cached ids named the old topic,
    // DashboardGrid's binding resolver finds the new one by name.
    void dropResolvedIds();
    void emitChanged();

    bool m_updating = false;
    QVector<DeviceOption> m_devices;
    QComboBox* m_deviceCombo = nullptr;
    QComboBox* m_topicCombo = nullptr;
    QLineEdit* m_sendCommandEdit = nullptr;
    QLineEdit* m_userNameEdit = nullptr;
    QCheckBox* m_ownOnRightCheck = nullptr;

    // Carried through untouched: the resolved binding ids and the optional
    // sample period.
    QString m_boundTopicName;
    QString m_sourceIdHex;
    QString m_topicIdHex;
    QJsonValue m_sampleTimeMs;
};

}  // namespace traceview
