#include "protocol/telemetryfieldrouter.h"

#include <QStringDecoder>

#include <btp/telemetry.hpp>

namespace traceview {

namespace {

// ProtocolRouter already split schema_version off the sample -- sample.payload
// is the encoded_body alone.
constexpr btp::SampleLayout kBodyOnly = btp::SampleLayout::BodyOnly;

const std::uint8_t* bytesOf(const QByteArray& payload) {
    return reinterpret_cast<const std::uint8_t*>(payload.constData());
}

}  // namespace

TelemetryFieldRouter::TelemetryFieldRouter(TelemetryCatalog* catalog, QObject* parent)
    : QObject(parent), m_catalog(catalog) {}

void TelemetryFieldRouter::onTelemetrySample(const TelemetrySample& sample) {
    const TelemetryTopicSchema* schema =
        m_catalog ? m_catalog->lookup(sample.sourceId, sample.topicId, sample.schemaVersion)
                  : nullptr;
    if (schema == nullptr) {
        // telemetry.md section 15: unknown / unannounced schema -- reject the
        // sample, don't guess at another version or encoding. topico 16
        // PASSO 9: tell ManifestClient so it can request an update instead of
        // this happening silently forever.
        ++m_diagnostics.schemaUnknown;
        emit unknownSchema(sample.sourceId, sample.topicId, sample.schemaVersion);
        emit diagnosticsChanged();
        return;
    }

    const QVector<btp::FieldSpec>& specs = schema->orderedFieldSpecs();
    const std::size_t specCount = std::size_t(specs.size());
    const std::uint8_t* data = bytesOf(sample.payload);
    const std::size_t size = std::size_t(sample.payload.size());

    if (schema->encoding == TelemetryEncoding::Utf8) {
        // Whole-body UTF8 (telemetry.md section 12.2): the body is the sample
        // payload as-is; Qt validates the text.
        btp::SampleReader reader(data, size, specs.data(), specCount, btp::kEncodingUtf8,
                                 kBodyOnly);
        btp::ByteView body{};
        if (reader.body(&body) != btp::MessageError::Ok) {
            ++m_diagnostics.decodeErrors;
            emit diagnosticsChanged();
            return;
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder.decode(QByteArrayView(body.data, qsizetype(body.size)));
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

    std::uint8_t encoding = 0;
    if (schema->encoding == TelemetryEncoding::PackedLe) {
        encoding = btp::kEncodingPackedLe;
    } else if (schema->encoding == TelemetryEncoding::TlvLe) {
        encoding = btp::kEncodingTlvLe;
    } else {
        // OPAQUE_BYTES / JSON_UTF8 / CSV_UTF8 are not consumed by the UI;
        // reject rather than guess (each has different validation rules).
        ++m_diagnostics.decodeErrors;
        emit diagnosticsChanged();
        return;
    }

    // Pass 1: validate the whole sample. telemetry.md section 14.4 forbids a
    // partial decode, so nothing is emitted until finish() confirms the body
    // is structurally sound and fully consumed.
    {
        btp::SampleReader reader(data, size, specs.data(), specCount, encoding, kBodyOnly);
        if (reader.finish() != btp::MessageError::Ok) {
            ++m_diagnostics.decodeErrors;
            emit diagnosticsChanged();
            return;
        }
    }
    ++m_diagnostics.samplesDecoded;

    // Pass 2: emit one fieldSample() per present element. The payload is
    // stable, so a second reader over the same bytes is free of allocation.
    btp::SampleReader reader(data, size, specs.data(), specCount, encoding, kBodyOnly);
    btp::SampleValue value{};
    TelemetryFieldBinding binding;
    binding.sourceId = sample.sourceId;
    binding.topicId = sample.topicId;
    while (reader.next(&value) == btp::SampleStep::Item) {
        if (value.is_null) {
            // telemetry.md section 7.2 / 14.3: no value this sample -- don't
            // fabricate zero or reuse a previous element.
            continue;
        }
        binding.fieldId = value.field->field_id;
        for (std::uint16_t i = 0; i < value.count; ++i) {
            binding.elementIndex = i;
            emit fieldSample(binding, sample.timestampUs, value.f64(i));
        }
    }
    emit diagnosticsChanged();
}

}  // namespace traceview
