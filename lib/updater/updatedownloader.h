#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace traceview {

// Finds `assetName`'s hash in `checksumsData` (a SHA256SUMS.txt body: one
// "<hex hash>  <filename>" line per asset, the format sha256sum/CertUtil
// both write). Returns an empty string if no line's *last*
// whitespace-separated token matches `assetName` exactly
// (case-insensitively) -- including when two lines have been run together
// with no newline between them, which merges a line's filename into the
// next line's hash token instead of leaving it last. Exposed as a free
// function so this parsing is unit-testable without a live download --
// see tests/test_updatedownloader.cpp.
QString findChecksum(const QByteArray& checksumsData, const QString& assetName);

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
