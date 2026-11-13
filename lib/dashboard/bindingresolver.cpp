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

const QLatin1String kFieldNameKey("fieldName");
const QLatin1String kUnitKey("unit");
const QLatin1String kMinKey("min");
const QLatin1String kMaxKey("max");
const QLatin1String kXAxisKey("xAxis");
const QLatin1String kYAxisKey("yAxis");

// A gauge has "series" too, but one range for all its rings, at the top
// level; a chart's range is per series (ChartSeriesConfig::declaredMin/Max).
bool isChartConfig(const QJsonObject& config) {
    return config.contains(kXAxisKey) || config.contains(kYAxisKey) ||
           !(config.contains(kMinKey) || config.contains(kMaxKey));
}

// Copies what `topic` now declares for the series' field -- unit and value
// range -- over the copy the series was saved with. A field the topic
// doesn't have keeps its copy; so does an unnamed one when `idsMoved`.
void refreshSeriesMetadata(QJsonObject& series, const CatalogTopicInfo& topic, bool idsMoved) {
    const QString fieldName = series.value(kFieldNameKey).toString();
    const CatalogTopicField* field = nullptr;
    if (!fieldName.isEmpty()) {
        field = findFieldByName(topic, fieldName);
    } else if (!idsMoved) {
        field = findFieldById(topic, series.value(kFieldIdKey).toInt(0));
    }
    if (!field) {
        return;
    }
    series[kUnitKey] = field->unit;
    const auto setOrRemove = [&series](const QLatin1String& key, double value) {
        if (qIsNaN(value)) {
            series.remove(key);
        } else {
            series[key] = value;
        }
    };
    setOrRemove(kMinKey, field->minValue);
    setOrRemove(kMaxKey, field->maxValue);
}

// The union of every series' declared range, as ChartConfigEditor presets
// the shared Y axis from it -- false unless every series declares both ends.
bool declaredRangeUnion(const QJsonArray& series, double* outMin, double* outMax) {
    if (series.isEmpty()) {
        return false;
    }
    double lo = 0.0;
    double hi = 0.0;
    for (int i = 0; i < series.size(); ++i) {
        const QJsonObject entry = series.at(i).toObject();
        if (!entry.contains(kMinKey) || !entry.contains(kMaxKey)) {
            return false;
        }
        const double entryMin = entry.value(kMinKey).toDouble();
        const double entryMax = entry.value(kMaxKey).toDouble();
        lo = i == 0 ? entryMin : qMin(lo, entryMin);
        hi = i == 0 ? entryMax : qMax(hi, entryMax);
    }
    *outMin = lo;
    *outMax = hi;
    return lo < hi;
}

// The Y range spin boxes hold two decimals.
bool sameAxisValue(double a, double b) {
    return qAbs(a - b) < 0.005;
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
        const bool chart = isChartConfig(config);
        const QJsonArray before = out.value(kSeriesKey).toArray();
        QJsonArray series = before;
        for (int i = 0; i < series.size(); ++i) {
            QJsonObject entry = series.at(i).toObject();
            resolveFields(entry, *topic, idsMoved);
            if (chart) {
                refreshSeriesMetadata(entry, *topic, idsMoved);
            }
            series[i] = entry;
        }
        out[kSeriesKey] = series;

        // A Y range that was the declared one (the editor's preset) follows
        // the declaration; any other is the user's own and stays.
        QJsonObject yAxis = out.value(kYAxisKey).toObject();
        double oldMin = 0.0;
        double oldMax = 0.0;
        double newMin = 0.0;
        double newMax = 0.0;
        if (chart && yAxis.contains(kMinKey) && yAxis.contains(kMaxKey) &&
            declaredRangeUnion(before, &oldMin, &oldMax) &&
            declaredRangeUnion(series, &newMin, &newMax) &&
            sameAxisValue(yAxis.value(kMinKey).toDouble(), oldMin) &&
            sameAxisValue(yAxis.value(kMaxKey).toDouble(), oldMax) &&
            !(sameAxisValue(oldMin, newMin) && sameAxisValue(oldMax, newMax))) {
            yAxis[kMinKey] = newMin;
            yAxis[kMaxKey] = newMax;
            out[kYAxisKey] = yAxis;
        }
    }
    return out;
}

}  // namespace traceview
