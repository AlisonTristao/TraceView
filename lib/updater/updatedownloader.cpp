#include "updater/updatedownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <memory>

namespace traceview {

QString findChecksum(const QByteArray& checksumsData, const QString& assetName) {
    const QString checksumsText = QString::fromUtf8(checksumsData);
    for (const QString& line : checksumsText.split('\n', Qt::SkipEmptyParts)) {
        const QStringList parts =
            line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.last().compare(assetName, Qt::CaseInsensitive) == 0) {
            return parts.first().toLower();
        }
    }
    return QString();
}

UpdateDownloader::UpdateDownloader(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
}

void UpdateDownloader::download(const QUrl& assetUrl, const QString& assetName,
                                 const QUrl& checksumsUrl) {
#if defined(TRACEVIEW_FLATPAK_BUILD)
    emit failed(tr("Updates are managed by Flatpak. Use 'flatpak update'."));
    return;
#endif
    m_assetUrl = assetUrl;
    m_assetName = assetName;
    m_checksumsUrl = checksumsUrl;
    downloadChecksums();
}

void UpdateDownloader::downloadChecksums() {
    QNetworkReply* reply = m_manager->get(QNetworkRequest(m_checksumsUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(tr("Couldn't download SHA256SUMS.txt: %1").arg(reply->errorString()));
            return;
        }
        // sha256sum/CertUtil line format: "<hex hash>  <filename>".
        const QString expectedHash = findChecksum(reply->readAll(), m_assetName);
        if (expectedHash.isEmpty()) {
            emit failed(tr("%1 is not listed in SHA256SUMS.txt.").arg(m_assetName));
            return;
        }
        downloadAsset(expectedHash);
    });
}

QString UpdateDownloader::targetDirectory() {
#if defined(Q_OS_ANDROID)
    // The package installer reads the APK through Qt's FileProvider
    // (${applicationId}.qtprovider), whose paths cover the app's files/ dir
    // but not cache/ -- which is where TempLocation points on Android.
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           QStringLiteral("/TraceViewUpdate");
#else
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/TraceViewUpdate");
#endif
}

void UpdateDownloader::downloadAsset(const QString& expectedHash) {
    const QString dir = targetDirectory();
    QDir().mkpath(dir);
    const QString filePath = dir + QStringLiteral("/") + m_assetName;

    // Only one update is ever kept: drop leftovers of older releases (an APK
    // is tens of MB of the phone's app storage).
    for (const QString& name : QDir(dir).entryList(QDir::Files)) {
        if (name != m_assetName) {
            QFile::remove(dir + QStringLiteral("/") + name);
        }
    }
    // Already downloaded on an earlier launch (the user picked "Later")?
    // Reuse it if it still matches the published hash, instead of pulling
    // the whole asset over mobile data again.
    {
        QFile existing(filePath);
        if (existing.open(QIODevice::ReadOnly)) {
            QCryptographicHash existingHash(QCryptographicHash::Sha256);
            const bool read = existingHash.addData(&existing);
            existing.close();
            if (read && QString::fromLatin1(existingHash.result().toHex())
                                .compare(expectedHash, Qt::CaseInsensitive) == 0) {
                emit finished(filePath);
                return;
            }
        }
    }

    auto* file = new QSaveFile(filePath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        emit failed(tr("Couldn't write %1").arg(filePath));
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto writeOk = std::make_shared<bool>(true);

    QNetworkReply* reply = m_manager->get(QNetworkRequest(m_assetUrl));
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);
    auto drain = [reply, file, hash, writeOk]() {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        hash->addData(chunk);
        if (*writeOk && file->write(chunk) != chunk.size()) {
            *writeOk = false;
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, hash, writeOk, drain, expectedHash, filePath]() {
                reply->deleteLater();
                file->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    file->cancelWriting();
                    emit failed(
                        tr("Couldn't download %1: %2").arg(m_assetName, reply->errorString()));
                    return;
                }
                drain();  // whatever arrived after the last readyRead
                if (!*writeOk) {
                    file->cancelWriting();
                    emit failed(tr("Couldn't write %1").arg(filePath));
                    return;
                }
                const QString actualHash = QString::fromLatin1(hash->result().toHex());
                if (actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0) {
                    file->cancelWriting();
                    emit failed(tr("Checksum mismatch for %1 -- the download may be corrupted.")
                                    .arg(m_assetName));
                    return;
                }
                if (!file->commit()) {
                    emit failed(tr("Couldn't write %1").arg(filePath));
                    return;
                }
                emit finished(filePath);
            });
}

}  // namespace traceview
