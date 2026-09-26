#include "protocol/manifeststore.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace traceview {

namespace {

constexpr char kSuffix[] = ".btpm";

// A MANIFEST_DATA payload is bounded by the logical-payload ceilings BTP
// negotiates (a few KB); anything far past that on disk is not ours.
constexpr qint64 kMaxFileBytes = 256 * 1024;

}  // namespace

ManifestStore& ManifestStore::instance() {
    static ManifestStore store;
    return store;
}

QString ManifestStore::directory() const {
    if (!m_directory.isEmpty()) {
        return m_directory;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/manifests");
}

void ManifestStore::setDirectory(const QString& directory) {
    m_directory = directory;
    m_loaded.clear();
}

QString ManifestStore::fileFor(quint32 sourceId) const {
    return directory() + QLatin1Char('/') +
           QStringLiteral("%1").arg(sourceId, 8, 16, QChar('0')).toUpper() +
           QLatin1String(kSuffix);
}

const QByteArray* ManifestStore::find(quint32 sourceId) {
    const auto it = m_loaded.constFind(sourceId);
    if (it != m_loaded.constEnd()) {
        return &it.value();
    }
    QFile file(fileFor(sourceId));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaxFileBytes) {
        return nullptr;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        return nullptr;
    }
    return &m_loaded.insert(sourceId, bytes).value();
}

void ManifestStore::store(quint32 sourceId, const QByteArray& payload) {
    if (payload.isEmpty() || payload.size() > kMaxFileBytes) {
        return;
    }
    m_loaded.insert(sourceId, payload);
    if (!QDir().mkpath(directory())) {
        return;  // still served from memory for the rest of this run
    }
    // All-or-nothing on disk: a crash mid-write leaves the previous file, never
    // a truncated one (which the Node would reject anyway, costing a refetch).
    QSaveFile file(fileFor(sourceId));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(payload);
        file.commit();
    }
}

void ManifestStore::evict(quint32 sourceId) {
    m_loaded.remove(sourceId);
    QFile::remove(fileFor(sourceId));
}

void ManifestStore::clear() {
    m_loaded.clear();
    QDir dir(directory());
    const QStringList files =
        dir.entryList({QStringLiteral("*") + QLatin1String(kSuffix)}, QDir::Files);
    for (const QString& name : files) {
        dir.remove(name);
    }
}

int ManifestStore::count() const {
    return QDir(directory())
        .entryList({QStringLiteral("*") + QLatin1String(kSuffix)}, QDir::Files)
        .size();
}

QVector<quint32> ManifestStore::sourceIds() const {
    QVector<quint32> out;
    const QStringList files = QDir(directory()).entryList(
        {QStringLiteral("*") + QLatin1String(kSuffix)}, QDir::Files);
    for (const QString& name : files) {
        bool ok = false;
        const quint32 id = name.chopped(int(sizeof(kSuffix)) - 1).toUInt(&ok, 16);
        if (ok) {
            out.append(id);
        }
    }
    return out;
}

}  // namespace traceview
