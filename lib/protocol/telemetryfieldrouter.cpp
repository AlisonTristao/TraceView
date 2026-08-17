#include "protocol/telemetryfieldrouter.h"

#include "protocol/packedledecoder.h"

namespace traceview {

TelemetryFieldRouter::TelemetryFieldRouter(TelemetryCatalog* catalog, QObject* parent)
    : QObject(parent), m_catalog(catalog) {}

void TelemetryFieldRouter::onTelemetrySample(const TelemetrySample& sample) {
    const TelemetryTopicSchema* schema =
        m_catalog ? m_catalog->lookup(sample.sourceId, sample.topicId, sample.schemaVersion) : nullptr;
    if (schema == nullptr) {
        // TELEMETRY.md section 6.4: unknown/unannounced schema -- reject the
        // sample, don't guess at another version or encoding. topico 16
        // PASSO 9: tell ManifestClient so it can request an update instead
        // of this happening silently forever.
        ++m_diagnostics.schemaUnknown;
        emit unknownSchema(sample.sourceId, sample.topicId, sample.schemaVersion);
        emit diagnosticsChanged();
        return;
    }
    if (schema->encoding != TelemetryEncoding::PackedLe) {
        // Only PACKED_LE has a decoder in this topico (see
        // telemetrycatalog.h) -- a schema declaring another encoding is
        // rejected rather than guessed at.
        ++m_diagnostics.decodeErrors;
        emit diagnosticsChanged();
        return;
    }

    QHash<quint16, TelemetryFieldValue> values;
    if (!decodePackedLe(*schema, sample.payload, &values)) {
        ++m_diagnostics.decodeErrors;
        emit diagnosticsChanged();
        return;
    }
    ++m_diagnostics.samplesDecoded;

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const TelemetryFieldValue& value = it.value();
        if (value.isNull) {
            // TELEMETRY.md section 8: no value this sample -- don't
            // fabricate zero or reuse a previous element.
            continue;
        }
        TelemetryFieldBinding binding;
        binding.sourceId = sample.sourceId;
        binding.topicId = sample.topicId;
        binding.fieldId = it.key();
        for (int i = 0; i < value.elements.size(); ++i) {
            binding.elementIndex = quint16(i);
            emit fieldSample(binding, sample.timestampUs, value.elements[i]);
        }
    }
    emit diagnosticsChanged();
}

}  // namespace traceview
