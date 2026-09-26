#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "telemetry/deviceinforecord.h"

namespace btp {
class Node;
struct NodeManifest;
struct NodeManifestOutcome;
}  // namespace btp

namespace traceview {

class TelemetryCatalog;
struct TelemetryTopicSchema;

// Turns MANIFEST_DATA into the TelemetryCatalog everything downstream decodes
// against (BTP/docs/commands.md section 3).
//
// Since BTP 2.48.0 the wire half is btp::Node's: requests go out through
// Node::request_manifest[_cached]() -- sealed or not, and under which
// identity, is the Node's NodeConfig (BtpBackend) deciding, the same as every
// other send -- and every MANIFEST_DATA SUCCESS comes back through
// Node::on_manifest(), which this class installs itself as. That is also
// where the manifest cache lives (NodeConfig::manifest_load()/store(), backed
// by ManifestStore): a source asked for with requestCatalogFor() carries the
// cached revision, and a NOT_MODIFIED answer arrives here already paired with
// the cached topics.
//
// What stays here is TraceView's side: parsing topic/field records into
// TelemetryTopicSchema, the per-source revision/boot bookkeeping, the
// unknown-schema re-request cooldown, and the Qt signals.
//
// Every MANIFEST_DATA is a complete, self-contained descriptor of exactly one
// source (commands.md section 3) -- applied the moment it arrives; there is
// no need to buffer an enumeration until CATALOG_COMPLETE.
class ManifestClient : public QObject {
    Q_OBJECT

public:
    // Installs itself as `node`'s on_manifest() callback; `node` must outlive
    // this object.
    ManifestClient(btp::Node& node, TelemetryCatalog* catalog, QObject* parent = nullptr);
    ~ManifestClient() override;

public slots:
    // Console session (serial/USB to a dongle): a fresh target=0 enumeration,
    // only when the dongle's own catalog revision (HELLO_RESULT's
    // config_revision) differs from what this process last saw -- on a
    // reconnect to the same, unchanged catalog, the TelemetryCatalog contents
    // still in memory are trusted as-is.
    void onSessionEstablished(quint32 peerConfigRevision);

    // Asks ONE source for its catalog, with the cached revision when there is
    // one (so an unchanged source answers a ~60-byte NOT_MODIFIED). A hub
    // answers an enumeration with every device it has heard of, while a robot
    // only ever has its own catalog to give and a child device only ever wants
    // that one. Zero (the enumeration wildcard) is refused.
    void requestCatalogFor(quint32 sourceId);

    // A full target=0 enumeration, unconditionally. Never uses the cache --
    // known_config_revision names one source's revision.
    void requestFullCatalog();

    // Wired to TelemetryFieldRouter::unknownSchema: a sample whose (source,
    // topic, schema_version) is not in the catalog triggers a targeted
    // re-request for just that source, rate-limited per source so a steady
    // sample stream cannot flood the link while a reply is in flight.
    void onUnknownSchema(quint32 sourceId, quint16 topicId, quint16 schemaVersion);

public:
    // BtpBackend forwards NodeRx::ManifestRejected here: clears that target's
    // cooldown so a legitimate later retry (once the source actually shows
    // up) is not blocked by an attempt that answered nothing.
    void onManifestRejected(const btp::NodeManifestOutcome& outcome);

    // Registers whatever ManifestStore holds for `sourceId` -- topic schemas
    // only, no boot_id (that is per boot, and only a live answer carries it,
    // so a SUBSCRIBE still waits for one). Lets a device's topics show up in
    // the widget editors before it ever connects. No-op with the cache off or
    // nothing stored. Returns whether anything was registered.
    bool primeFromCache(quint32 sourceId);

    // ManifestStore's copy of `sourceId`, parsed but registered nowhere --
    // what that source described on its last connection, for UI shown while
    // it is offline. False with the cache off, nothing stored, or an entry
    // that is not a complete manifest of that source.
    static bool readCached(quint32 sourceId, QVector<TelemetryTopicSchema>* topics,
                           QVector<DeviceInfoRecord>* info);

    // The cached source whose source_info "name" is `name` (case-insensitive,
    // trimmed), or 0 when none -- or more than one -- is. How a device reached
    // directly (whose source_id only its own HELLO_RESULT gives) finds its
    // cache entry while disconnected.
    static quint32 cachedSourceNamed(const QString& name);

    // One delivered manifest, exactly as Node::on_manifest() hands it over.
    // Public so a test can drive the parse side without a wire.
    void applyManifest(const btp::NodeManifest& manifest);

signals:
    // After a SUCCESS MANIFEST_DATA was applied: topic schemas added/replaced
    // and/or that source's current boot_id recorded (NOT_MODIFIED too, since
    // the boot can change without the catalog changing). SubscriptionManager
    // listens for this to send any SUBSCRIBE held back for lack of a boot.
    void catalogUpdated();

    // Every applied SUCCESS (full or NOT_MODIFIED) for a specific source. A hub
    // child has no HELLO_RESULT, so this is the only place its card's
    // "reported by device" identity can come from.
    void sourceDescribed(quint32 sourceId, quint32 bootId, quint32 configRevision);

    // The source_info block (commands.md 3.12) of a SUCCESS for `sourceId`,
    // when it carried entries. Rides NOT_MODIFIED too (not covered by
    // config_revision).
    void sourceInfoReported(quint32 sourceId, const QVector<traceview::DeviceInfoRecord>& info);

private:
    btp::Node& m_node;
    TelemetryCatalog* m_catalog;

    bool m_haveDongleConfigRevision = false;
    quint32 m_lastDongleConfigRevision = 0;

    QHash<quint32, qint64>
        m_lastRequestMsBySource;  // sourceId -> epoch ms of last unknown-schema request
};

}  // namespace traceview
