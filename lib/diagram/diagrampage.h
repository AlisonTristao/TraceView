#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

#include "devices/device.h"

namespace traceview {

class DiagramScene;
class DiagramScriptRuntime;
class DiagramView;

// The Control tab's content: a Simulink-style canvas (DiagramScene/
// DiagramView) that mirrors DevicesGrid's own device list one-to-one -- every
// device already configured in the Devices tab shows up here as a block,
// automatically, the moment its device exists (see setDevices()). Each block
// owns a live DiagramScriptRuntime (see that class): MainWindow calls
// feedTelemetry()/feedTerminal() as that device's Backend produces traffic,
// and connects commandRequested()/terminalInRequested() back to
// Backend::sendCommand()/sendTerminalIn() -- this class never touches
// Backend/DeviceConnection directly, keeping traceview_diagram independent
// of traceview_protocol, same layering traceview_devices already follows.
// The diagram's own connections (arrows between blocks) carry no runtime
// data yet -- purely documentary for this pass. No .tvproj persistence yet:
// scripts are kept in memory only, for the lifetime of the open tab.
class DiagramPage : public QWidget {
    Q_OBJECT

public:
    explicit DiagramPage(QWidget* parent = nullptr);

    // Adds a block (and a script runtime) for any device that doesn't have
    // one yet, updates every existing block's label/link-state dot, and
    // removes blocks (and their runtime) whose device is gone. Called by
    // MainWindow whenever DevicesGrid's own list changes, same fan-out as
    // refreshOtaTabDevices()/refreshPropertiesPanelDevices() -- including
    // once right after this page is first created, so a device added before
    // the tab was ever opened still shows up immediately.
    void setDevices(const QVector<Device>& devices);

    // Forwarded to `deviceId`'s script runtime, if it has one -- a no-op
    // otherwise (e.g. a sample for a device whose block hasn't been added
    // yet, which shouldn't happen once setDevices() has run, but a stray
    // signal delivered between device-add and the next setDevices() call is
    // harmless to drop).
    void feedTelemetry(const QString& deviceId, quint16 topicId, quint16 fieldId,
                       quint16 elementIndex, double value, quint64 timestampUs);
    void feedTerminal(const QString& deviceId, const QString& text);

    // Serializes every block's position + script and every connection, for
    // ProjectStore's "diagram" section. Devices themselves are NOT
    // duplicated here -- they're addressed by id, resolved against whatever
    // DevicesGrid's own "devices" section already restored.
    QJsonObject toJson() const;
    // Restores positions/scripts/connections saved by toJson(). Call after
    // setDevices() has already populated blocks for the current device list
    // -- an entry for a deviceId with no block (deleted since, or not
    // loaded yet) is silently skipped, same convention as
    // DevicesGrid::fromJson() skipping an unparseable device.
    void fromJson(const QJsonObject& object);

signals:
    // A block's script called device.sendCommand()/device.sendTerminal() --
    // MainWindow relays this to that device's real Backend.
    void commandRequested(const QString& deviceId, const QString& text);
    void terminalInRequested(const QString& deviceId, const QString& text);

private:
    void addBlockForDevice(const Device& device);
    void onBlockActivated(const QString& deviceId);

    DiagramScene* m_scene;
    DiagramView* m_view;
    QVector<Device> m_devices;
    // In-memory per-block script text, keyed by Device::id -- shown back in
    // DiagramBlockConfigDialog on reopen. The runtime (below) is the
    // authoritative, currently-running copy; this is only the editor's
    // starting point.
    QHash<QString, QString> m_scripts;
    QHash<QString, DiagramScriptRuntime*> m_runtimes;
    int m_placedCount = 0;
};

}  // namespace traceview
