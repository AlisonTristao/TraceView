#include "protocol/logfilereader.h"

#include <QFile>
#include <QIODevice>
#include <btp/codec.hpp>

namespace traceview {

namespace {

quint16 readU16LE(const std::uint8_t* bytes) {
    return quint16(bytes[0]) | (quint16(bytes[1]) << 8);
}

LogEntry toEntry(const btp::Header& header, const QByteArray& payload) {
    LogEntry entry;
    entry.timestampUs = header.timestamp_us;
    entry.sourceId = header.source_id;
    entry.bootId = header.boot_id;
    entry.sequence = header.sequence;
    entry.severity = static_cast<LogSeverity>(static_cast<quint8>(header.object_id));
    entry.message = QString::fromUtf8(payload);
    return entry;
}

}  // namespace

bool LogFileReader::load(const QString& filePath) {
    m_entries.clear();
    m_lastError.clear();
    m_skippedFrameCount = 0;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = file.errorString();
        return false;
    }

    const QByteArray bytes = file.readAll();
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
    const std::size_t total = std::size_t(bytes.size());

    // Set once a fragment with fragment_index == 0 starts a multi-frame
    // record; cleared once the last fragment completes it. A frame that
    // doesn't extend the record currently being accumulated ends it instead
    // of being folded in -- see the class comment for why this is enough
    // without btp::Reassembler's out-of-order handling.
    bool hasPending = false;
    btp::Header pendingHeader{};
    QByteArray pendingPayload;
    quint8 pendingNextIndex = 0;

    auto dropPending = [&]() {
        if (hasPending) {
            ++m_skippedFrameCount;
        }
        hasPending = false;
        pendingPayload.clear();
        pendingNextIndex = 0;
    };

    std::size_t offset = 0;
    while (offset + btp::kV1HeaderSize <= total) {
        const std::size_t payloadSize = readU16LE(data + offset + 10);
        const std::size_t frameSize = btp::kV1HeaderSize + payloadSize + btp::kV1CrcSize;
        if (offset + frameSize > total) {
            // Truncated trailing frame -- e.g. the robot lost power
            // mid-flush. Whatever parsed before this point is still kept;
            // note it rather than silently dropping it.
            m_lastError = QStringLiteral("Truncated frame at end of file (%1 bytes ignored)")
                              .arg(total - offset);
            break;
        }

        btp::DecodedFrame decoded;
        const btp::Error error =
            btp::decode(data + offset, frameSize, btp::kEspNowTransport, &decoded);
        offset += frameSize;

        if (error != btp::Error::Ok) {
            ++m_skippedFrameCount;
            dropPending();
            continue;
        }
        if (decoded.header.type != btp::MessageType::Log) {
            // .blog files only ever contain Log frames (see class comment);
            // ignore anything else rather than misinterpreting it as one.
            continue;
        }

        const QByteArray payload(reinterpret_cast<const char*>(decoded.payload.data),
                                 int(decoded.payload.size));

        if (decoded.header.fragment_count <= 1) {
            dropPending();
            m_entries.append(toEntry(decoded.header, payload));
            continue;
        }

        const bool extendsPending = hasPending &&
                                    pendingHeader.source_id == decoded.header.source_id &&
                                    pendingHeader.boot_id == decoded.header.boot_id &&
                                    pendingHeader.sequence == decoded.header.sequence &&
                                    decoded.header.fragment_index == pendingNextIndex;

        if (decoded.header.fragment_index == 0) {
            dropPending();
            hasPending = true;
            pendingHeader = decoded.header;
            pendingPayload = payload;
            pendingNextIndex = 1;
        } else if (extendsPending) {
            pendingPayload += payload;
            ++pendingNextIndex;
        } else {
            ++m_skippedFrameCount;
            dropPending();
            continue;
        }

        if (hasPending && pendingNextIndex == pendingHeader.fragment_count) {
            m_entries.append(toEntry(pendingHeader, pendingPayload));
            hasPending = false;
            pendingPayload.clear();
            pendingNextIndex = 0;
        }
    }

    dropPending();  // an incomplete record at EOF never becomes an entry
    return true;
}

}  // namespace traceview
