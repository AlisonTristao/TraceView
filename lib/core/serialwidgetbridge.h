#pragma once

#include <QObject>
#include <QtGlobal>

namespace traceview {

class BtpSession;
class DashboardGrid;
class DashboardWidget;
class ProtocolRouter;
class SerialManager;

// Wires each control/serial-monitor widget's output to the shared
// connection as soon as it's created (BACKEND_TODO.txt Tasks 9/10):
// PushButtonWidget/ToggleSwitchWidget/SliderWidget's sendRequested() (a
// fully-formed command) still goes through SerialManager::writeCommand()
// (appends the configured line terminator, docs/PROTOCOL.md "Outbound:
// control commands" -- migrating that path onto BTP COMMAND is a separate,
// later topico).
//
// SerialMonitorWidget (the terminal) is different: as of topico 19
// ("terminal protocolado"), its sendRequested() (raw keystrokes/escape
// sequences) is wrapped as a BTP TERMINAL_IN frame and handed to
// BtpSession::sendFrame() instead of SerialManager::write() -- it no longer
// touches the wire directly, and no longer receives SerialManager::
// dataReceived() at all (that stream is BTP-framed binary, not text; a
// terminal fed those raw bytes would render TELEMETRY as garbage
// characters, exactly the CRITERIO DE ACEITE topico 19 forbids). Instead
// each terminal widget is fed only ProtocolRouter::terminalFrameReceived()
// payloads carrying TERMINAL_OUT.
//
// Hooks DashboardGrid::widgetCreated() rather than rebuilding an index off
// itemsChanged() the way SerialDataRouter does: outbound wiring has no key
// to go stale, so a one-shot per-instance connect at construction time is
// enough, and avoids double-connecting a widget that persists across an
// unrelated itemsChanged() (e.g. some other item's key edit).
class SerialWidgetBridge : public QObject {
    Q_OBJECT

public:
    SerialWidgetBridge(SerialManager* serialManager, BtpSession* btpSession, ProtocolRouter* protocolRouter,
                       DashboardGrid* grid, QObject* parent = nullptr);

private:
    void wireWidget(DashboardWidget* widget);
    void sendTerminalIn(const QByteArray& bytes);

    SerialManager* m_serialManager;
    BtpSession* m_btpSession;
    ProtocolRouter* m_protocolRouter;

    // Minimal, self-contained BTP identity for TERMINAL_IN frames only --
    // there is no HELLO/MANIFEST exchange yet (topicos 15-17) to learn a
    // negotiated one from, mirroring the dongle's own "no persisted
    // identity yet" stance for boot_id (see topico 13 RESULTADO). Random,
    // non-zero, generated once per MainWindow lifetime.
    quint32 m_terminalSourceId;
    quint32 m_terminalBootId;
    quint32 m_terminalSequence = 0;
};

} // namespace traceview
