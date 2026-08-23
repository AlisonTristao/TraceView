#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// One field of a CatalogTopicInfo's schema -- the display-only mirror of
// protocol/telemetrycatalog.h's TelemetryFieldSchema. Generic value type
// (moved out of traceview_protocol, same reasoning as TopicSubscriptionState
// above) so dashboard/devices code can show a device's reported fields
// without depending on traceview_protocol: `type`/`unit` are already
// human-readable strings here, not the wire enum/scale/offset.
struct CatalogTopicField {
    quint16 fieldId = 0;
    QString name;
    QString type;  // e.g. "float32", "uint8[4]"
    QString unit;
};

// One (source_id, topic_id, schema_version) schema a Backend's catalog
// currently holds, as announced by the device's own manifest exchange
// (BTP's MANIFEST_DATA for BtpBackend). telemetry.md section 3 requires
// every topic to declare a stable, human-readable `name` alongside its
// numeric topic_id -- this is that name, surfaced generically so the Devices
// panel's gear icon (DeviceConfigDialog) and chart/gauge config editors can
// show it without depending on traceview_protocol.
struct CatalogTopicInfo {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 schemaVersion = 0;
    QString name;
    QString encoding;  // e.g. "PACKED_LE"
    QVector<CatalogTopicField> fields;
};

}  // namespace traceview
