#pragma once

#include <QObject>
#include <QtGlobal>

#include "protocol/telemetrycatalog.h"
#include "protocol/telemetrysample.h"
#include "telemetry/telemetrybinding.h"

namespace traceview {

// Decodes each TelemetrySample against a TelemetryCatalog schema and
// broadcasts one fieldSample() per (field, present element). Two widgets/
// consumers each connect independently to this same Qt signal to consume
// the same field -- CRITERIO DE ACEITE "dois widgets podem consumir o mesmo
// campo" -- there is no per-binding subscriber list to manage here, unlike
// the actual wire-level SUBSCRIBE bookkeeping topico 17 adds; this is a
// plain in-process fan-out. The origin's timestamp_us travels with every
// emission untouched -- CRITERIO DE ACEITE "o timestamp nao e descartado no
// roteador" (PLANO_GERAL.txt decision 11: the dongle/client MUST NOT
// substitute arrival time for it, and this router never does).
class TelemetryFieldRouter : public QObject {
    Q_OBJECT

public:
    struct Diagnostics {
        quint64 samplesDecoded = 0;
        quint64 schemaUnknown = 0;   // no (source, topic, schema_version) match
        quint64 decodeErrors = 0;    // schema known, payload structurally invalid
                                      // or an encoding this topico can't decode
    };

    explicit TelemetryFieldRouter(TelemetryCatalog* catalog, QObject* parent = nullptr);

    const Diagnostics& diagnostics() const { return m_diagnostics; }

public slots:
    // Connect to ProtocolRouter::telemetrySampleReceived in production; also
    // safe to call directly in tests.
    void onTelemetrySample(const traceview::TelemetrySample& sample);

signals:
    void fieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs, double value);
    void diagnosticsChanged();
    // A TELEMETRY sample arrived for (sourceId, topicId, schemaVersion) with
    // no matching catalog entry -- topico 16 PASSO 9: "rejeitar amostra com
    // schema_version desconhecida e solicitar atualizacao" instead of
    // guessing. ManifestClient listens for this to (rate-limited) re-request
    // that source's manifest rather than silently dropping every sample
    // forever.
    void unknownSchema(quint32 sourceId, quint16 topicId, quint16 schemaVersion);

private:
    TelemetryCatalog* m_catalog;
    Diagnostics m_diagnostics;
};

}  // namespace traceview
