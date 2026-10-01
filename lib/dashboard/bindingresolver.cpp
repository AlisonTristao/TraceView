#include "bindingresolver.h"

#include <QJsonArray>
#include <QStringList>

#include "dashboard/widgetconfigeditor.h"

namespace traceview {

namespace {

const QLatin1String kSourceIdKey("sourceId");
const QLatin1String kTopicIdKey("topicId");
const QLatin1String kTopicNameKey("topicName");
const QLatin1String kSeriesKey("series");
const QLatin1String kFieldIdKey("fieldId");
const QLatin1String kFieldIdSuffix("FieldId");

quint32 configSourceId(const QJsonObject& config) {
    return quint32(config.value(kSourceIdKey).toString("0").toULongLong(nullptr, 0));
}

quint16 configTopicId(const QJsonObject& config) {
    return quint16(qBound(0, config.value(kTopicIdKey).toString("0").toInt(nullptr, 0), 65535));
}

// "fieldId" (a chart/gauge series, or a pre-multi-ring gauge's top-level
// one) and "<x>FieldId" (the audio widget's seq/rate/samples) are field
// references; their name lives next to them as "fieldName"/"<x>FieldName".
bool isFieldIdKey(const QString& key) {
    return key == kFieldIdKey || (key.endsWith(kFieldIdSuffix) && key.size() > 7);
}

QString fieldNameKeyFor(const QString& idKey) {
    return idKey.chopped(2) + QLatin1String("Name");
}

const CatalogTopicInfo* findTopicById(const QVector<CatalogTopicInfo>& catalog, quint32 sourceId,
                                      quint16 topicId) {
    for (const CatalogTopicInfo& topic : catalog) {
        if (topic.sourceId == sourceId && topic.topicId == topicId) {
            return &topic;
        }
    }
    return nullptr;
}

// The topic named `name`: on `currentSourceId` if it has one (the ids are
// still right), else on the device's own `selfSourceId`, else the only
// source that has it. Several sources and no preference (a hub's catalog
// can hold more than one robot) is ambiguous -- nullptr, ids stay.
const CatalogTopicInfo* findTopicByName(const QVector<CatalogTopicInfo>& catalog,
                                        const QString& name, quint32 currentSourceId,
                                        quint32 selfSourceId) {
    QVector<const CatalogTopicInfo*> candidates;
    for (const CatalogTopicInfo& topic : catalog) {
        if (topic.name == name) {
            candidates.append(&topic);
        }
    }
    for (const quint32 preferred : {currentSourceId, selfSourceId}) {
        if (preferred == 0) {
            continue;
        }
        for (const CatalogTopicInfo* topic : candidates) {
            if (topic->sourceId == preferred) {
                return topic;
            }
        }
    }
    for (const CatalogTopicInfo* topic : candidates) {
        if (topic->sourceId != candidates.first()->sourceId) {
            return nullptr;
        }
    }
    return candidates.isEmpty() ? nullptr : candidates.first();
}

const CatalogTopicField* findFieldById(const CatalogTopicInfo& topic, int fieldId) {
    for (const CatalogTopicField& field : topic.fields) {
        if (field.fieldId == fieldId) {
            return &field;
        }
    }
    return nullptr;
}

const CatalogTopicField* findFieldByName(const CatalogTopicInfo& topic, const QString& name) {
    for (const CatalogTopicField& field : topic.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

// Resolves every field reference held directly in `holder` against `topic`.
// `idsMoved`: the topic itself was just re-pointed by name, so an id without
// a name was numbered against another schema -- naming it from this one
// would be a guess, and it is left alone.
void resolveFields(QJsonObject& holder, const CatalogTopicInfo& topic, bool idsMoved) {
    const QStringList keys = holder.keys();
    for (const QString& idKey : keys) {
        if (!isFieldIdKey(idKey)) {
            continue;
        }
        const QString nameKey = fieldNameKeyFor(idKey);
        const int fieldId = holder.value(idKey).toInt(0);
        const QString fieldName = holder.value(nameKey).toString();
        if (!fieldName.isEmpty()) {
            const CatalogTopicField* field = findFieldByName(topic, fieldName);
            if (field && field->fieldId != fieldId) {
                holder[idKey] = int(field->fieldId);
            }
            continue;
        }
        if (idsMoved) {
            continue;
        }
        const CatalogTopicField* field = findFieldById(topic, fieldId);
        if (field && !field->name.isEmpty()) {
            holder[nameKey] = field->name;
        }
    }
}

}  // namespace

QJsonObject resolveBindingNames(const QJsonObject& config, const QVector<CatalogTopicInfo>& catalog,
                                quint32 selfSourceId) {
    if (catalog.isEmpty() || !config.contains(kTopicIdKey)) {
        return config;
    }
    const quint32 sourceId = configSourceId(config);
    const quint16 topicId = configTopicId(config);
    const QString topicName = config.value(kTopicNameKey).toString();

    QJsonObject out = config;
    const CatalogTopicInfo* topic = nullptr;
    bool idsMoved = false;
    if (!topicName.isEmpty()) {
        topic = findTopicByName(catalog, topicName, sourceId, selfSourceId);
        if (!topic) {
            return config;
        }
        if (topic->sourceId != sourceId) {
            out[kSourceIdKey] = formatHexId(topic->sourceId, 8);
            idsMoved = true;
        }
        if (topic->topicId != topicId) {
            out[kTopicIdKey] = formatHexId(topic->topicId, 4);
            idsMoved = true;
        }
    } else {
        if (topicId == 0) {
            return config;
        }
        topic = findTopicById(catalog, sourceId, topicId);
        if (!topic || topic->name.isEmpty()) {
            return config;
        }
        out[kTopicNameKey] = topic->name;
    }

    resolveFields(out, *topic, idsMoved);
    if (out.contains(kSeriesKey)) {
        QJsonArray series = out.value(kSeriesKey).toArray();
        for (int i = 0; i < series.size(); ++i) {
            QJsonObject entry = series.at(i).toObject();
            resolveFields(entry, *topic, idsMoved);
            series[i] = entry;
        }
        out[kSeriesKey] = series;
    }
    return out;
}

}  // namespace traceview
