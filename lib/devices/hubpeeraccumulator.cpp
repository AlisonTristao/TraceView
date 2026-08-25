#include "devices/hubpeeraccumulator.h"

namespace traceview {

namespace {

// Index into HubPeerAccumulator::m_columns. Order matches fieldNames().
enum Column { kChannel = 0, kSourceId, kBootId, kMac, kLastSeenMs, kOnline, kColumnCount };

// `mac` arrives as one flat uint8 array of six octets per peer, not as a
// per-peer array -- PACKED_LE has no nested arrays (telemetry.md 4.1).
constexpr int kMacOctets = 6;

QString formatMac(const QVector<double>& macColumn, int peerIndex) {
    const int base = peerIndex * kMacOctets;
    if (base + kMacOctets > macColumn.size()) {
        return QString();
    }
    QString mac;
    mac.reserve(kMacOctets * 3 - 1);
    for (int i = 0; i < kMacOctets; ++i) {
        if (i > 0) {
            mac += QLatin1Char(':');
        }
        mac += QStringLiteral("%1").arg(quint8(macColumn.at(base + i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return mac;
}

}  // namespace

QVector<QString> HubPeerAccumulator::fieldNames() {
    return {QStringLiteral("channel"),      QStringLiteral("source_id"), QStringLiteral("boot_id"),
            QStringLiteral("mac"),          QStringLiteral("last_seen_ms"), QStringLiteral("online")};
}

bool HubPeerAccumulator::resolve(const QHash<quint16, QString>& fieldIdToName) {
    const QVector<QString> names = fieldNames();

    QHash<quint16, int> mapping;
    for (auto it = fieldIdToName.constBegin(); it != fieldIdToName.constEnd(); ++it) {
        const int column = int(names.indexOf(it.value()));
        if (column >= 0) {
            mapping.insert(it.key(), column);
        }
    }
    // Size alone is not enough -- two ids naming the same field would reach
    // it while leaving another column unbound. Check every column is covered.
    QVector<bool> covered(names.size(), false);
    for (int column : mapping) {
        covered[column] = true;
    }
    if (covered.contains(false)) {
        m_columnByFieldId.clear();
        m_columns.clear();
        return false;
    }

    m_columnByFieldId = mapping;
    m_columns = QVector<QVector<double>>(names.size());
    return true;
}

void HubPeerAccumulator::append(quint16 fieldId, quint16 elementIndex, double value) {
    const auto it = m_columnByFieldId.constFind(fieldId);
    if (it == m_columnByFieldId.constEnd()) {
        return;
    }

    QVector<double>& column = m_columns[it.value()];
    // elementIndex 0 restarts the array. TelemetryFieldRouter emits a
    // field's elements in ascending index order within one sample, so this
    // is what shrinks a column when a peer drops off the dongle's table --
    // writing by index alone would leave the departed peer's stale values
    // behind forever.
    if (elementIndex == 0) {
        column.clear();
    }
    if (elementIndex != column.size()) {
        // Out of order, or a gap. The router produces neither, so this means
        // the sample is not the shape assumed above; drop the column rather
        // than storing values against the wrong peer index.
        column.clear();
        return;
    }
    column.append(value);
}

QVector<HubPeer> HubPeerAccumulator::peers() const {
    if (m_columns.size() != kColumnCount) {
        return {};
    }

    // How many peers this snapshot describes: the shortest of the arrays
    // that carry one element per peer. They should all agree, but a sample
    // read mid-update must not run past the end of any of them, and `mac` is
    // excluded because it holds six elements per peer, not one.
    int peerCount = m_columns.at(kChannel).size();
    for (int column : {int(kSourceId), int(kBootId), int(kLastSeenMs), int(kOnline)}) {
        peerCount = qMin(peerCount, m_columns.at(column).size());
    }

    QVector<HubPeer> peers;
    peers.reserve(peerCount);
    for (int i = 0; i < peerCount; ++i) {
        HubPeer peer;
        peer.channel = quint8(m_columns.at(kChannel).at(i));
        peer.sourceId = quint32(m_columns.at(kSourceId).at(i));
        peer.lastSeenAgeMs = quint32(m_columns.at(kLastSeenMs).at(i));
        peer.online = m_columns.at(kOnline).at(i) != 0.0;
        peer.mac = formatMac(m_columns.at(kMac), i);
        // boot_id is decoded (the dongle publishes it, and skipping the
        // column would misalign every slot after it) but has no HubPeer
        // field: nothing in the picker tells two peers apart by it, and
        // Device::peerSourceId is deliberately boot-independent.
        peers.append(peer);
    }
    return peers;
}

void HubPeerAccumulator::clearSamples() {
    for (QVector<double>& column : m_columns) {
        column.clear();
    }
}

}  // namespace traceview
