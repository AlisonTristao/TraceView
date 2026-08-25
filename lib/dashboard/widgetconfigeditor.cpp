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

void populateTopicCombo(QComboBox* combo, const QVector<DeviceOption>& devices, const QString& deviceId) {
    const QString previousText = combo->currentText();

    combo->clear();
    if (!deviceId.isEmpty()) {
        for (const DeviceOption& device : devices) {
            if (device.id != deviceId) {
                continue;
            }
            for (const CatalogTopicInfo& topic : device.catalogTopics) {
                const QString label = topic.name.isEmpty()
                                           ? QCoreApplication::translate("WidgetConfigEditor", "(unnamed topic)")
                                           : topic.name;
                const QString sourceHex = QString("0x%1").arg(topic.sourceId, 8, 16, QChar('0'));
                const QString topicHex = QString("0x%1").arg(topic.topicId, 4, 16, QChar('0'));
                combo->addItem(QString("%1 (%2 / %3)").arg(label, sourceHex, topicHex),
                               sourceHex + "|" + topicHex);
            }
            break;
        }
    }

    combo->setCurrentText(previousText);
}

bool decodeTopicComboData(const QVariant& itemData, QString* sourceIdHexOut, QString* topicIdHexOut) {
    const QString text = itemData.toString();
    const int sep = text.indexOf('|');
    if (sep < 0) {
        return false;
    }
    if (sourceIdHexOut) {
        *sourceIdHexOut = text.left(sep);
    }
    if (topicIdHexOut) {
        *topicIdHexOut = text.mid(sep + 1);
    }
    return true;
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

QString resolveSourceLabel(const QVector<DeviceOption>& devices, quint32 sourceId) {
    if (sourceId == 0) {
        return QString();
    }
    for (const DeviceOption& device : devices) {
        if (device.selfSourceId == sourceId) {
            return device.name;
        }
    }
    return QString();
}

QString formatHexId(quint32 value, int digits) {
    return QString("0x%1").arg(value, digits, 16, QChar('0')).toUpper();
}

QVector<CatalogTopicField> resolveCatalogTopicFields(const QVector<DeviceOption>& devices, const QString& deviceId,
                                                       const QString& sourceIdText, const QString& topicIdText) {
    if (deviceId.isEmpty()) {
        return {};
    }
    const quint32 sourceId = quint32(sourceIdText.toULongLong(nullptr, 0));
    const quint16 topicId = quint16(qBound(0, topicIdText.toInt(nullptr, 0), 65535));
    if (sourceId == 0 || topicId == 0) {
        return {};
    }
    for (const DeviceOption& device : devices) {
        if (device.id != deviceId) {
            continue;
        }
        for (const CatalogTopicInfo& topic : device.catalogTopics) {
            if (topic.sourceId == sourceId && topic.topicId == topicId) {
                return topic.fields;
            }
        }
        break;
    }
    return {};
}

void populateFieldCombo(QComboBox* combo, const QVector<CatalogTopicField>& fields) {
    const QString previousText = combo->currentText();

    combo->clear();
    for (const CatalogTopicField& field : fields) {
        const QString label = field.name.isEmpty()
                                   ? QCoreApplication::translate("WidgetConfigEditor", "Field %1").arg(field.fieldId)
                                   : QString("%1 (%2)").arg(field.name).arg(field.fieldId);
        combo->addItem(label, field.fieldId);
    }

    combo->setCurrentText(previousText);
}

} // namespace traceview
