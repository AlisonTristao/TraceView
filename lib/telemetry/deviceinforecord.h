#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace traceview {

// One entry of a device's MANIFEST_DATA source_info block (BTP's
// docs/commands.md section 3.12): three textual fields the device published
// about itself -- firmware version, chip, running partition, a configured
// name or description. `key` is a stable machine identifier ("fw_version"),
// `label` a human name for display ("Firmware", possibly empty -- fall back
// to `key`), `value` the datum as text.
//
// Generic value type in traceview_telemetry, same reasoning as
// CatalogTopicInfo: the Devices panel shows it without depending on
// traceview_protocol, and a from-scratch Backend can report it too. It is
// display-only and live session state -- never persisted (see
// Device::reportedInfo).
struct DeviceInfoRecord {
    QString key;
    QString label;
    QString value;
};

}  // namespace traceview

// Passed through Backend::deviceInfoReported (a QVector of these) and stored in
// QVariant by QSignalSpy in the tests -- both need the metatype registered.
Q_DECLARE_METATYPE(traceview::DeviceInfoRecord)
