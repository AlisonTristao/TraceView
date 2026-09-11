#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace traceview {

// Downloads one release asset plus the release's SHA256SUMS.txt (published
// alongside every asset by .github/workflows/release.yml) and refuses to
// hand back a file whose hash doesn't match -- the app is about to execute
// this file (Windows) or copy it over its own install directory (Linux), so
// a corrupted or truncated download must not be treated as good.
class UpdateDownloader : public QObject {
    Q_OBJECT

public:
    explicit UpdateDownloader(QObject* parent = nullptr);

    // checksumsUrl is the SHA256SUMS.txt asset from the same release as
    // assetUrl -- UpdateChecker resolves both the same way (by filename), so
    // the caller passes both along from the one UpdateInfo it already has.
    void download(const QUrl& assetUrl, const QString& assetName, const QUrl& checksumsUrl);

signals:
    void progress(qint64 received, qint64 total);
    // filePath is a copy of the verified asset under a TraceViewUpdate
    // temp directory, named assetName -- the caller (UpdateInstaller) owns
    // it from here.
    void finished(const QString& filePath);
    void failed(const QString& reason);

private:
    void downloadChecksums();
    void downloadAsset();
    void verifyAndFinish();

    QNetworkAccessManager* m_manager;
    QUrl m_assetUrl;
    QString m_assetName;
    QUrl m_checksumsUrl;
    QByteArray m_assetData;
    QByteArray m_checksumsData;
};

}  // namespace traceview
