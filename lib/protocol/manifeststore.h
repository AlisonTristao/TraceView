#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// Where learned manifests live between sessions -- the storage half of BTP
// 2.48.0's manifest cache (btp::NodeConfig::manifest_load()/store()/evict(),
// docs/library.md 16.4). btp::Node decides WHEN to read and write; this
// decides WHERE: one file per source under directory(), holding the raw
// MANIFEST_DATA payload exactly as it came off the wire (the wire format is
// the serialisation -- the Node validates whatever comes back before
// trusting it, so a corrupt or foreign file is evicted, never applied).
//
// Keyed by source_id, shared by every BtpBackend in the process: the same
// robot reached over serial, a hub or TCP/BLE finds the same entry. That
// shared index is also what "same device over several links" builds on.
//
// GUI-thread only, like every BtpBackend.
class ManifestStore {
public:
    static ManifestStore& instance();

    ManifestStore() = default;
    ManifestStore(const ManifestStore&) = delete;
    ManifestStore& operator=(const ManifestStore&) = delete;

    // Default: QStandardPaths::AppDataLocation + "/manifests". Changing it
    // drops what is held in memory (tests point this at a temp dir).
    QString directory() const;
    void setDirectory(const QString& directory);

    // Off: the Node sees no cache at all (NodeConfig::has_manifest_cache()
    // false) and asks for the whole manifest every session, as before 2.48.
    bool enabled() const {
        return m_enabled;
    }
    void setEnabled(bool enabled) {
        m_enabled = enabled;
    }

    // btp::Node::set_manifest_skip_on_hello(): on a direct TCP/BLE session,
    // trust the cache without asking when HELLO_RESULT's config_revision
    // matches it. Off by default -- and off is the recommendation: the
    // NOT_MODIFIED that skipping saves is ~60 bytes and is the only thing that
    // refreshes source_info (firmware version etc.).
    bool skipOnHello() const {
        return m_skipOnHello;
    }
    void setSkipOnHello(bool skip) {
        m_skipOnHello = skip;
    }

    // The stored payload for `sourceId`, or nullptr. The pointer stays valid
    // until the next store()/evict()/clear()/setDirectory() -- which is what
    // NodeConfig::manifest_load()'s view contract needs.
    const QByteArray* find(quint32 sourceId);
    void store(quint32 sourceId, const QByteArray& payload);
    void evict(quint32 sourceId);

    // Removes every stored manifest (Settings -> "Clear manifest cache").
    void clear();
    // Entries on disk right now.
    int count() const;
    // The source_id of every entry on disk, in no particular order.
    QVector<quint32> sourceIds() const;

private:
    QString fileFor(quint32 sourceId) const;

    QString m_directory;  // empty until first use -> the default
    bool m_enabled = true;
    bool m_skipOnHello = false;
    // Loaded lazily, one file at a time; a cached miss is not remembered, so
    // a file dropped in by another instance is still found.
    QHash<quint32, QByteArray> m_loaded;
};

}  // namespace traceview
