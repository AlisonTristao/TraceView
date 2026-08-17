#include "protocol/protocolrouter.h"

namespace traceview {

ProtocolRouter::ProtocolRouter(QObject* parent) : QObject(parent) {}

void ProtocolRouter::onFrameReceived(const BtpFrame& frame) {
    switch (frame.type) {
        case btp::MessageType::Telemetry: {
            // TELEMETRY.md section 2: the payload's first 2 octets are
            // schema_version, little-endian; everything after is
            // encoded_body, still fully opaque.
            if (frame.payload.size() < 2) {
                ++m_diagnostics.telemetryDropped;
                break;
            }
            TelemetrySample sample;
            sample.sourceId = frame.sourceId;
            sample.bootId = frame.bootId;
            sample.sequence = frame.sequence;
            sample.timestampUs = frame.timestampUs;
            sample.topicId = frame.objectId;
            sample.schemaVersion =
                quint16(quint8(frame.payload[0])) | (quint16(quint8(frame.payload[1])) << 8);
            sample.payload = frame.payload.mid(2);
            ++m_diagnostics.telemetryRouted;
            emit telemetrySampleReceived(sample);
            break;
        }
        case btp::MessageType::Log:
            ++m_diagnostics.logRouted;
            emit logFrameReceived(frame);
            break;
        case btp::MessageType::Command:
            ++m_diagnostics.commandRouted;
            emit commandFrameReceived(frame);
            break;
        case btp::MessageType::Terminal:
            ++m_diagnostics.terminalRouted;
            emit terminalFrameReceived(frame);
            break;
        case btp::MessageType::Control:
            ++m_diagnostics.controlRouted;
            emit controlFrameReceived(frame);
            break;
        case btp::MessageType::Invalid:
        default:
            // btp::decode() already rejects Invalid/reserved types before a
            // BtpFrame can exist (BTP_V1.md section 4); counted here too in
            // case a future MessageType value reaches this router before a
            // case is added for it.
            ++m_diagnostics.unknownTypeDropped;
            break;
    }
    emit diagnosticsChanged();
}

}  // namespace traceview
