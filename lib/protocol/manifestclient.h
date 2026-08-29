#pragma once

#include <QHash>
#include <QObject>
#include <QtGlobal>

namespace traceview {

class BtpSession;
class ProtocolRouter;
class TelemetryCatalog;
struct BtpFrame;

// Replaces topico 15's temporary "assume every new source_id speaks
// bally_software's hardcoded schemas" bridge (see registerBallySoftwareCatalog()
// in telemetrycatalog.h) with a real MANIFEST_REQUEST/MANIFEST_DATA exchange
// (BTP/docs/commands.md section 3), topico 16 PASSOS 6/7/9.
//
// Wire identity: this class owns a small private (source_id, boot_id)
// generated once per process, the same pattern topico 19's
// SerialWidgetBridge already established for TERMINAL_IN -- it does not need
// to be the same identity BtpHandshake used for HELLO (MANIFEST_REQUEST's
// correlation is just "the dongle echoes back whatever (source_id, boot_id,
// sequence) this request carried," independent of the session's own HELLO
// identity).
//
// Every MANIFEST_DATA this dongle sends (whether it is one entry of a
// target=0 enumeration or the answer to a targeted request) is a complete,
// self-contained descriptor of exactly one source
// (commands.md section 3) -- so onControlFrameReceived() applies
// each one the moment it arrives; there is no need to buffer an enumeration
// until CATALOG_COMPLETE before any of it becomes usable.
class ManifestClient : public QObject {
    Q_OBJECT

public:
    explicit ManifestClient(BtpSession* session, ProtocolRouter* router, TelemetryCatalog* catalog,
                            QObject* parent = nullptr);

public slots:
    // Wired to BtpHandshake::sessionEstablished(peerConfigRevision) --
    // PASSO 6's "solicitar manifesto apenas quando revisao mudar": a fresh
    // target=0 enumeration is only sent when the dongle's own catalog
    // revision (HELLO_RESULT's config_revision) differs from what this
    // process last saw from that dongle; on a reconnect to the same,
    // unchanged catalog, the existing TelemetryCatalog contents (still held
    // in memory, MainWindow never clears them on reconnect) are trusted
    // as-is.
    void onSessionEstablished(quint32 peerConfigRevision);
    // Asks ONE source for its catalog, instead of enumerating everything the
    // other end knows about. That distinction is the whole difference between
    // talking to a hub and talking through one: a hub answers an enumeration
    // with every device it has heard of, while a robot only ever has its own
    // catalog to give and a child device only ever wants that one.
    void requestCatalogFor(quint32 sourceId);

    // A full target=0 enumeration, unconditionally (no revision-skip guard --
    // that is onSessionEstablished()'s job). BtpBackend calls this from its
    // keepalive tick while the dongle's catalog is still empty, because the
    // single enumeration on sessionEstablished can be lost and nothing else
    // re-asks -- and without the dongle's hub.peers schema, MainWindow can
    // never resolve a hub child's presence or its peer list.
    void requestFullCatalog();

    // Wired to TelemetryFieldRouter::unknownSchema -- PASSO 9: a sample
    // whose (source, topic, schema_version) is not in the catalog triggers a
    // targeted re-request for just that source (carrying whatever revision
    // is already cached for it, so an unrelated stale-schema guess never
    // happens: the dongle either confirms NOT_MODIFIED or sends the real
    // update), rate-limited per source so a steady sample stream cannot
    // flood the link with duplicate requests while a reply is in flight.
    void onUnknownSchema(quint32 sourceId, quint16 topicId, quint16 schemaVersion);

signals:
    // Emitted after a successful MANIFEST_DATA response was applied to the
    // catalog: topic schemas added/replaced, and/or that source's current
    // boot_id recorded (which happens on NOT_MODIFIED too, since the boot can
    // change without the catalog changing). topico 17's SubscriptionManager
    // listens for this to send any SUBSCRIBE it had to hold back for lack of
    // a target_boot_id.
    void catalogUpdated();

    // Emitted whenever a SUCCESS MANIFEST_DATA (full or NOT_MODIFIED) for a
    // specific source was applied. A hub child has no HELLO_RESULT, so this is
    // the only place its card's "reported by device" identity can come from --
    // BtpBackend forwards it as deviceIdentified() when describedSourceId is
    // its own peer.
    void sourceDescribed(quint32 sourceId, quint32 bootId, quint32 configRevision);

private slots:
    void onControlFrameReceived(const traceview::BtpFrame& frame);

private:
    void sendRequest(quint32 targetSourceId, quint32 targetBootId, quint32 knownRevision);

    BtpSession* m_session;
    TelemetryCatalog* m_catalog;

    quint32 m_clientSourceId;
    quint32 m_clientBootId;
    quint32 m_nextSequence = 1;

    bool m_haveDongleConfigRevision = false;
    quint32 m_lastDongleConfigRevision = 0;

    QHash<quint32, quint32> m_sourceRevisions;  // sourceId -> last known config_revision
    QHash<quint32, qint64>
        m_lastRequestMsBySource;  // sourceId -> epoch ms of last targeted request
};

}  // namespace traceview
