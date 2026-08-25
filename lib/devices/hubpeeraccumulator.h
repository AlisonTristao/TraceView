#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "devices/device.h"
#include "telemetry/telemetrybinding.h"

namespace traceview {

// Reassembles the dongle's `hub.peers` topic into HubPeer rows.
//
// That topic is six parallel variable-length arrays (bally_dongle's
// DonglePublisher.h, kPeersFields) and it reaches a client one element at a
// time: TelemetryFieldRouter emits one fieldSample() per (field, element),
// so a single 16-peer sample arrives as up to 176 separate emissions. A
// HubPeer row needs several of those fields at once, hence an accumulator
// rather than a straight-through decode.
//
// Lives in traceview_devices next to HubPeer itself, not in MainWindow,
// for two reasons. It keeps decode logic out of UI code (CONTRIBUTING.md's
// transport/protocol/presentation split), and it makes the reassembly
// testable without a QWidget -- see tests/test_hubpeeraccumulator.cpp.
// MainWindow keeps only the half that genuinely needs a Backend: resolving
// the topic out of a device's catalog and holding the subscription.
//
// Depends on traceview_telemetry (TelemetryFieldBinding) but NOT on
// traceview_protocol/BTP: what arrives here is already a generic
// (field, element, value) triple, same reasoning as CatalogTopicInfo.
class HubPeerAccumulator {
public:
    // The six field names this expects, in the order their columns are
    // stored. Matched by NAME, never by number: telemetry.md section 1
    // makes topic and field ids local to a source's namespace, so they are
    // the dongle's to renumber and only these strings are a contract
    // between the two repositories.
    static QVector<QString> fieldNames();

    // Binds wire field ids to columns from a device's announced schema.
    // Returns false, leaving this unresolved, unless every one of
    // fieldNames() is present -- a topic named hub.peers that is missing an
    // array is not one this can decode, and a peer row with no source_id is
    // exactly what must never reach Device::peerSourceId.
    bool resolve(const QHash<quint16, QString>& fieldIdToName);
    bool isResolved() const { return !m_columnByFieldId.isEmpty(); }

    // One TelemetryFieldRouter emission. Ignores a field id that isn't part
    // of this topic's schema.
    void append(quint16 fieldId, quint16 elementIndex, double value);

    // Whatever has been accumulated so far, as complete rows. Empty until a
    // first full sample has arrived.
    QVector<HubPeer> peers() const;

    // Forgets the accumulated sample but keeps the resolved field mapping --
    // what a session drop calls for, since the schema survives it.
    void clearSamples();

private:
    QHash<quint16, int> m_columnByFieldId;   // wire field id -> index into m_columns
    QVector<QVector<double>> m_columns;      // one per fieldNames() entry
};

}  // namespace traceview
