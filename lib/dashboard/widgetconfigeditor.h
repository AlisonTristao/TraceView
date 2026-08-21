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
};

// Resolves `sourceIdText`/`topicIdText` (as typed into a Source/Topic field
// -- hex ("0x...") or decimal, same convention chartdata.cpp's
// parseSourceId()/parseTopicId() use for the persisted JSON) against
// `deviceId`'s catalogTopics entry within `devices`. Returns an empty string
// if the device is unknown, its catalog hasn't arrived yet, or nothing in it
// matches -- callers fall back to showing the raw hex in that case.
QString resolveCatalogTopicName(const QVector<DeviceOption>& devices, const QString& deviceId,
                                 const QString& sourceIdText, const QString& topicIdText);

// Repopulates `combo` from `devices`: a leading "(No device)" entry (empty
// id, meaning "unbound" -- same interpretation as ChartConfigEditor's
// existing sourceId==0/topicId==0 "not subscribed") followed by one entry
// per device, id stored as itemData. Preserves the current selection by id
// if it's still present in the new list, otherwise falls back to "(No
// device)". Shared by every editor with a Device field (chart/gauge/
// control/terminal) instead of duplicating this per editor.
void populateDeviceCombo(QComboBox* combo, const QVector<DeviceOption>& devices);

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
    virtual void setAvailableDevices(const QVector<DeviceOption>& devices) { Q_UNUSED(devices); }

signals:
    // Emitted whenever the user edits something; the new value is fetched
    // via config() rather than passed here, same shape as the rest of the
    // panel's *ChangeRequested signals.
    void configChanged();
};

} // namespace traceview
