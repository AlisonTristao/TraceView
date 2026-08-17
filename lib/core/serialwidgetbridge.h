#pragma once

#include <QObject>
#include <QtGlobal>

namespace traceview {

class Backend;
class DashboardGrid;
class DashboardWidget;
class SerialManager;

// Wires each control/serial-monitor widget's output to the shared
// connection as soon as it's created (BACKEND_TODO.txt Tasks 9/10):
// PushButtonWidget/ToggleSwitchWidget/SliderWidget's sendRequested() (a
// fully-formed command) goes through SerialManager::writeCommand() directly
// (appends the configured line terminator, docs/PROTOCOL.md "Outbound:
// control commands") -- this is raw text with no protocol envelope, so it
// bypasses Backend entirely.
//
// SerialMonitorWidget (the terminal) is different: as of topico 19
// ("terminal protocolado"), its sendRequested() (raw keystrokes/escape
// sequences) is handed to Backend::sendTerminalIn() instead of
// SerialManager::write() -- it no longer touches the wire directly, and no
// longer receives SerialManager::dataReceived() at all (that stream is
// protocol-framed binary, not text; a terminal fed those raw bytes would
// render telemetry as garbage characters, exactly the CRITERIO DE ACEITE
// topico 19 forbids). Instead each terminal widget is fed only
// Backend::terminalDataReceived() payloads -- whatever framing/filtering
// (e.g. BTP's TERMINAL_IN/TERMINAL_OUT distinction) that requires is the
// Backend implementation's job, not this bridge's.
//
// Hooks DashboardGrid::widgetCreated() rather than rebuilding an index off
// itemsChanged() the way SerialDataRouter does: outbound wiring has no key
// to go stale, so a one-shot per-instance connect at construction time is
// enough, and avoids double-connecting a widget that persists across an
// unrelated itemsChanged() (e.g. some other item's key edit).
class SerialWidgetBridge : public QObject {
    Q_OBJECT

public:
    SerialWidgetBridge(SerialManager* serialManager, Backend* backend, DashboardGrid* grid,
                       QObject* parent = nullptr);

private:
    void wireWidget(DashboardWidget* widget);

    SerialManager* m_serialManager;
    Backend* m_backend;
};

} // namespace traceview
