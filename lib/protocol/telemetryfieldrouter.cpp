#include "protocol/telemetryfieldrouter.h"

#include <QStringDecoder>

#include "protocol/packedledecoder.h"

namespace traceview {

TelemetryFieldRouter::TelemetryFieldRouter(TelemetryCatalog* catalog, QObject* parent)
    : QObject(parent), m_catalog(catalog) {}

void TelemetryFieldRouter::onTelemetrySample(const TelemetrySample& sample) {
    const TelemetryTopicSchema* schema =
        m_catalog ? m_catalog->lookup(sample.sourceId, sample.topicId, sample.schemaVersion)
                  : nullptr;
    if (schema == nullptr) {
        // telemetry.md section 6: unknown/unannounced schema -- reject the
        // sample, don't guess at another version or encoding. topico 16
        // PASSO 9: tell ManifestClient so it can request an update instead
        // of this happening silently forever.
        ++m_diagnostics.schemaUnknown;
        emit unknownSchema(sample.sourceId, sample.topicId, sample.schemaVersion);
        emit diagnosticsChanged();
        return;
    }
    if (schema->encoding == TelemetryEncoding::Utf8) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder.decode(sample.payload);
        if (decoder.hasError()) {
            ++m_diagnostics.decodeErrors;
            emit diagnosticsChanged();
            return;
        }
        ++m_diagnostics.samplesDecoded;
        emit textSample(sample.sourceId, sample.topicId, sample.timestampUs, text);
        emit diagnosticsChanged();
        return;
    }
    if (schema->encoding != TelemetryEncoding::PackedLe) {
        // UTF8 and PACKED_LE are the encodings currently consumed by the UI.
        // A schema declaring another encoding is rejected rather than guessed
        // at (JSON/CSV/TLV each has different validation rules).
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
            // telemetry.md section 8: no value this sample -- don't
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
