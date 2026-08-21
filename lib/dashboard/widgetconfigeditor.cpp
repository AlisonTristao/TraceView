#include "widgetconfigeditor.h"

#include <QComboBox>
#include <QCoreApplication>

namespace traceview {

void populateDeviceCombo(QComboBox* combo, const QVector<DeviceOption>& devices) {
    const QString previousId = combo->currentData().toString();

    combo->clear();
    combo->addItem(QCoreApplication::translate("WidgetConfigEditor", "(No device)"), QString());
    for (const DeviceOption& device : devices) {
        combo->addItem(device.name.isEmpty() ? QCoreApplication::translate("WidgetConfigEditor", "(Unnamed device)")
                                              : device.name,
                        device.id);
    }

    const int idx = combo->findData(previousId);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString resolveCatalogTopicName(const QVector<DeviceOption>& devices, const QString& deviceId,
                                 const QString& sourceIdText, const QString& topicIdText) {
    if (deviceId.isEmpty()) {
        return QString();
    }
    const quint32 sourceId = quint32(sourceIdText.toULongLong(nullptr, 0));
    const quint16 topicId = quint16(qBound(0, topicIdText.toInt(nullptr, 0), 65535));
    if (sourceId == 0 || topicId == 0) {
        return QString();
    }
    for (const DeviceOption& device : devices) {
        if (device.id != deviceId) {
            continue;
        }
        for (const CatalogTopicInfo& topic : device.catalogTopics) {
            if (topic.sourceId == sourceId && topic.topicId == topicId) {
                return topic.name;
            }
        }
        break;
    }
    return QString();
}

} // namespace traceview
