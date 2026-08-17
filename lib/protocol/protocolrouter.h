#pragma once

#include <QObject>

#include "protocol/btpframe.h"
#include "protocol/telemetrysample.h"

namespace traceview {

// Dispatches validated BtpFrame instances by MessageType (BTP_V1.md section
// 4) to one signal per logical channel, so nothing downstream has to
// re-check `type`/`objectId` itself -- the desktop-client counterpart of the
// dongle-side ProtocolRouter described in topico 12's RESULTADO, minus the
// FreeRTOS queues (this is single-threaded Qt event-loop code).
//
// TELEMETRY frames get one extra step: the first two bytes of the payload
// are schema_version (TELEMETRY.md section 2), split out into
// TelemetrySample::schemaVersion so nothing downstream re-parses it, and the
// remaining bytes are handed on as-is -- still opaque, never converted to
// UTF-8 or otherwise interpreted here (PLANO_GERAL.txt decision 6/8).
// LOG/COMMAND/TERMINAL/CONTROL payload parsing is out of scope for this
// topico (14's PASSOS are telemetry/data-model only); those channels are
// forwarded as raw BtpFrame for a future topico to interpret.
class ProtocolRouter : public QObject {
    Q_OBJECT

public:
    struct Diagnostics {
        quint64 telemetryRouted = 0;
        quint64 logRouted = 0;
        quint64 commandRouted = 0;
        quint64 terminalRouted = 0;
        quint64 controlRouted = 0;
        quint64 telemetryDropped = 0;  // payload shorter than the 2-byte
                                        // schema_version prefix
        quint64 unknownTypeDropped = 0;  // Invalid or a reserved MessageType
    };

    explicit ProtocolRouter(QObject* parent = nullptr);

    const Diagnostics& diagnostics() const { return m_diagnostics; }

public slots:
    // Connect to BtpSession::frameReceived in production; also safe to call
    // directly in tests without a real BtpSession/SerialManager.
    void onFrameReceived(const traceview::BtpFrame& frame);

signals:
    void telemetrySampleReceived(const traceview::TelemetrySample& sample);
    void logFrameReceived(const traceview::BtpFrame& frame);
    void commandFrameReceived(const traceview::BtpFrame& frame);
    void terminalFrameReceived(const traceview::BtpFrame& frame);
    void controlFrameReceived(const traceview::BtpFrame& frame);
    void diagnosticsChanged();

private:
    Diagnostics m_diagnostics;
};

}  // namespace traceview
