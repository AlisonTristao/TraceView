#include "updater/updatedownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

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
        m_checksumsData = reply->readAll();
        downloadAsset();
    });
}

void UpdateDownloader::downloadAsset() {
    QNetworkReply* reply = m_manager->get(QNetworkRequest(m_assetUrl));
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(tr("Couldn't download %1: %2").arg(m_assetName, reply->errorString()));
            return;
        }
        m_assetData = reply->readAll();
        verifyAndFinish();
    });
}

void UpdateDownloader::verifyAndFinish() {
    // sha256sum/CertUtil line format: "<hex hash>  <filename>".
    QString expectedHash;
    const QString checksumsText = QString::fromUtf8(m_checksumsData);
    for (const QString& line : checksumsText.split('\n', Qt::SkipEmptyParts)) {
        const QStringList parts =
            line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.last().compare(m_assetName, Qt::CaseInsensitive) == 0) {
            expectedHash = parts.first().toLower();
            break;
        }
    }

    if (expectedHash.isEmpty()) {
        emit failed(tr("%1 is not listed in SHA256SUMS.txt.").arg(m_assetName));
        return;
    }

    const QString actualHash = QString::fromLatin1(
        QCryptographicHash::hash(m_assetData, QCryptographicHash::Sha256).toHex());
    if (actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0) {
        emit failed(tr("Checksum mismatch for %1 -- the download may be corrupted.")
                        .arg(m_assetName));
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                         QStringLiteral("/TraceViewUpdate");
    QDir().mkpath(dir);
    const QString filePath = dir + QStringLiteral("/") + m_assetName;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit failed(tr("Couldn't write %1").arg(filePath));
        return;
    }
    file.write(m_assetData);
    file.close();

    emit finished(filePath);
}

}  // namespace traceview
