#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>

#include "telemetry/catalogtopicinfo.h"

class QComboBox;

namespace traceview {

// One device a widget's config editor can offer in a Device picker -- id is
// what's actually stored in the widget's JSON config (the "deviceId" key),
// name is display-only. A tiny local value type rather than reusing
// devices::Device: traceview_dashboard and traceview_devices are sibling
// libraries with no dependency between them (see lib/CMakeLists.txt) --
// MainWindow, which depends on both, converts DevicesGrid::devices() into
// these before handing them to PropertiesPanel::setAvailableDevices().
struct DeviceOption {
    QString id;
    QString name;
    // This device's Backend::catalogTopics() as of the last refresh -- lets
    // a config editor resolve the sourceId/topicId hex the user typed into
    // the human-readable name TELEMETRY.md section 3 requires every topic to
    // declare. Empty until that device has completed a manifest exchange at
    // least once (or if it isn't a telemetry-capable device at all).
    QVector<CatalogTopicInfo> catalogTopics;
    // The BTP source_id this device's own connection speaks as -- for a
    // TransportType::HubChannel device, that's the robot's persisted
    // Device::peerSourceId (known even before it ever connects); for
    // Serial/UsbHid, it's the peer's reported identity from the last
    // HELLO_RESULT (Device::btpId, live session state, empty/0 until
    // connected at least once). Zero means unknown. Lets a config editor's
    // Source field show a device NAME instead of a bare hex id -- see
    // resolveSourceLabel() below.
    quint32 selfSourceId = 0;
};

// Resolves `sourceIdText`/`topicIdText` (as typed into a Source/Topic field
// -- hex ("0x...") or decimal, same convention chartdata.cpp's
// parseSourceId()/parseTopicId() use for the persisted JSON) against
// `deviceId`'s catalogTopics entry within `devices`. Returns an empty string
// if the device is unknown, its catalog hasn't arrived yet, or nothing in it
// matches -- callers fall back to showing the raw hex in that case.
QString resolveCatalogTopicName(const QVector<DeviceOption>& devices, const QString& deviceId,
                                const QString& sourceIdText, const QString& topicIdText);

// The name of the device whose own identity (DeviceOption::selfSourceId) is
// `sourceId`, searched across every device in `devices` regardless of which
// one is currently selected in a Source/Topic field -- a chart's Device
// picker and the source_id its data actually comes from are two different
// questions once a hub is involved (see docs/DEVICES.md's Hub section), so
// this deliberately isn't scoped to one device the way
// resolveCatalogTopicName() is. Returns an empty string if `sourceId` is
// zero or doesn't match any configured device -- callers fall back to
// showing the raw hex in that case.
QString resolveSourceLabel(const QVector<DeviceOption>& devices, quint32 sourceId);

// Formats a BTP id the way every "reported identity" field in this codebase
// already does (Device::peerSourceId, BtpBackend's own btpId) -- "0x" plus
// `digits` uppercase hex digits, zero-padded. Shared so a config editor's
// fallback-to-hex display matches that convention instead of inventing its
// own.
QString formatHexId(quint32 value, int digits);

// Repopulates `combo` from `devices`: a leading "(No device)" entry (empty
// id, meaning "unbound" -- same interpretation as ChartConfigEditor's
// existing sourceId==0/topicId==0 "not subscribed") followed by one entry
// per device, id stored as itemData. Preserves the current selection by id
// if it's still present in the new list, otherwise falls back to "(No
// device)". Shared by every editor with a Device field (chart/gauge/
// control/terminal) instead of duplicating this per editor.
void populateDeviceCombo(QComboBox* combo, const QVector<DeviceOption>& devices);

// Repopulates `combo` (an editable QComboBox) with one entry per catalog
// topic `deviceId` has reported within `devices` -- label combines the
// topic's readable name with its source_id/topic_id hex (same format as
// deviceconfigdialog.cpp's catalogTopicLine()), itemData packs both ids as
// "0xSOURCE|0xTOPIC" for decodeTopicComboData() to split back out once the
// user picks one. Unlike populateDeviceCombo() this preserves the combo's
// current edit text verbatim rather than re-selecting by id: the field's
// whole point is to also accept a source/topic pair the catalog doesn't
// know about yet, and a manifest refresh must never clobber that.
void populateTopicCombo(QComboBox* combo, const QVector<DeviceOption>& devices,
                        const QString& deviceId);

// Splits a "0xSOURCE|0xTOPIC" itemData value (see populateTopicCombo) back
// into its two hex strings. Returns false, leaving both outputs untouched,
// if `itemData` isn't in that shape -- notably for index -1 (no selection).
bool decodeTopicComboData(const QVariant& itemData, QString* sourceIdHexOut,
                          QString* topicIdHexOut);

// Same lookup as resolveCatalogTopicName() above, but returning the matched
// topic's field list instead of its name -- what a chart/gauge series row's
// Field ID combo populates from (see populateFieldCombo()). Empty for the
// same reasons resolveCatalogTopicName() returns an empty string: unknown
// device, catalog not arrived yet, or no match.
QVector<CatalogTopicField> resolveCatalogTopicFields(const QVector<DeviceOption>& devices,
                                                     const QString& deviceId,
                                                     const QString& sourceIdText,
                                                     const QString& topicIdText);

// Repopulates `combo` (an editable QComboBox) with one entry per field of
// `fields` -- label combines the field's readable name with its numeric
// fieldId, itemData is the plain fieldId. Unlike populateDeviceCombo() this
// preserves the combo's current edit text verbatim rather than re-selecting
// by id (same reasoning as populateTopicCombo()): a series can bind a
// fieldId the device hasn't reported yet, and a catalog refresh must never
// clobber that. The combo's actual bound value lives in its "fieldId"
// property (see chartconfigeditor.cpp/gaugeconfigeditor.cpp's
// addSeriesRow()), never parsed back out of the displayed text.
void populateFieldCombo(QComboBox* combo, const QVector<CatalogTopicField>& fields);

// Base class for a widget type's type-specific settings, shown in the
// PropertiesPanel below the common Type/Name/Key fields (see
// PropertiesPanel, lib/core/propertiespanel.h). A type opts in by setting
// WidgetTypeInfo::configEditorFactory (lib/dashboard/widgetregistry.h) to a
// factory that builds one of these; types that don't register one simply
// show no config section. The panel owns the instance, calls setConfig()
// whenever the selection/config changes elsewhere (undo/redo, load), and
// listens for configChanged() to push edits back out — mirrors how
// DashboardWidget is the per-type extension point for the canvas cell
// itself, this is the analogous one for its properties editor.
class WidgetConfigEditor : public QWidget {
    Q_OBJECT

public:
    explicit WidgetConfigEditor(QWidget* parent = nullptr) : QWidget(parent) {}

    // One-way sync into the editor — must never emit configChanged().
    virtual void setConfig(const QJsonObject& config) = 0;

    virtual QJsonObject config() const = 0;

    // Pushes the current device list for editors with a Device picker to
    // repopulate their combo from -- called by PropertiesPanel right after
    // building a fresh editor, and again whenever the device list changes
    // while one is showing. Default no-op for editors with no such field.
    // One-way sync like setConfig() — must never emit configChanged().
    virtual void setAvailableDevices(const QVector<DeviceOption>& devices) {
        Q_UNUSED(devices);
    }

signals:
    // Emitted whenever the user edits something; the new value is fetched
    // via config() rather than passed here, same shape as the rest of the
    // panel's *ChangeRequested signals.
    void configChanged();
};

}  // namespace traceview
