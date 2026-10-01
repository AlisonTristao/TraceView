#pragma once

#include <QJsonObject>
#include <QVector>
#include <QtGlobal>

#include "telemetry/catalogtopicinfo.h"

namespace traceview {

// A telemetry widget's config (chart, gauge, text board, audio) binds to
// (deviceId, topic, field...) -- see docs/DASHBOARD.md "Telemetry bindings".
// The numeric ids in it ("sourceId", "topicId", "fieldId", "<x>FieldId") are
// not stable: the source id is derived from the device's MAC, and topic and
// field ids are handed out by the firmware in declaration order. The names
// next to them ("topicName", "fieldName", "<x>FieldName") are: they are what
// the firmware declares (TELEMETRY.md section 3), the same on a simulated
// device and on the real one. So the names are the binding and the ids are a
// cache of where those names currently live, still written so that older
// TraceView versions (which only read ids) keep working.
//
// Maps `config` onto `catalog` -- the topics the config's own device
// ("deviceId") currently reports, live or from the manifest cache -- and
// returns the result (equal to `config` when there is nothing to change):
//   - with a "topicName": the topic of that name is looked up (preferring the
//     config's current source, then `selfSourceId`, the device's own source
//     id, or 0 if unknown), and the ids are re-pointed at it -- the topic's
//     source/topic ids, and each field id from its field name. A field name
//     the topic doesn't have keeps its id. A topic name the catalog doesn't
//     have (or an ambiguous one: several sources, none preferred) leaves the
//     whole config as it was -- the ids are the fallback.
//   - without one (a project saved before names existed): the topic is
//     looked up by its ids and its name, plus the name of every field found
//     by id, is filled in, so the next save writes them. A binding the
//     catalog doesn't know keeps its ids and gains nothing.
// A config with no "topicId" key (not a telemetry widget) is returned as is,
// as is everything when `catalog` is empty. Pure function: no MainWindow, no
// Backend -- MainWindow installs it on DashboardGrid (setBindingResolver())
// with the catalog of whichever device the config names.
QJsonObject resolveBindingNames(const QJsonObject& config, const QVector<CatalogTopicInfo>& catalog,
                                quint32 selfSourceId);

}  // namespace traceview
