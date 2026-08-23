#pragma once

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace traceview {

class MdnsResolver;

// Thin QNetworkAccessManager wrapper for bally_OS's OTA HTTP side channel
// (lib/OTAUpdater in bally_OS): GET /status to poll reachability and POST
// /update to push a firmware binary. This is the first real network traffic
// in TraceView -- QtNetwork was linked into traceview_protocol before this
// only for QPasswordDigestor (see lib/CMakeLists.txt's own comment there).
//
// Requests are keyed by Device::id rather than by address, since OtaTab
// drives one of each in flight per row: a second checkStatus() for a device
// that already has one outstanding aborts the old QNetworkReply rather than
// letting two answers race each other back to the same row.
//
// A *.local address is resolved through MdnsResolver (our own multicast
// query, see its own header comment for why) before either request type
// touches QNetworkAccessManager at all -- Qt would otherwise hand it to the
// OS resolver, which is exactly the path that's unreliable on Windows.
// Resolved addresses are cached briefly (kMdnsCacheTtlMs) so a poll every
// few seconds doesn't re-query the network for every tick.
class OtaClient : public QObject {
    Q_OBJECT

public:
    explicit OtaClient(QObject* parent = nullptr);

    // GET http://<address>/status. `address` may be a bare IP, a regular
    // hostname, or a *.local mDNS name. Always resolves to exactly one
    // statusChecked() for this deviceId, including on a resolution/network
    // error or timeout (reachable=false) -- OtaTab's poll timer never needs
    // to guess whether a request went missing.
    void checkStatus(const QString& deviceId, const QString& address);

    // POST http://<address>/update with the file at `filePath` as the raw
    // body (matching OTAUpdater::handle_update_post -- no multipart) and
    // X-OTA-Password set to `password` (an empty password is fine; the robot
    // only checks the header if it has one configured). Emits uploadProgress
    // as the transfer proceeds and exactly one uploadFinished() at the end.
    void uploadFirmware(const QString& deviceId, const QString& address, const QString& password,
                         const QString& filePath);

signals:
    // otaReady/firmwareVersion are only meaningful when reachable is true;
    // see OTAUpdater.cpp's handle_status_get for the JSON shape this parses
    // ({"device","online","firmware","ota_ready"}). errorMessage is empty
    // when reachable is true, otherwise a human-readable reason -- e.g.
    // "Host not found" (a plain hostname/IP that didn't resolve, or a
    // *.local name our own mDNS query AND the OS resolver both failed on)
    // versus "Connection refused" (host is up but nothing is listening,
    // i.e. the robot hasn't entered OTA mode) versus a timeout. OtaTab
    // surfaces this as the status cell's tooltip since "Offline" alone
    // doesn't say which of those it is.
    void statusChecked(const QString& deviceId, bool reachable, bool otaReady, const QString& firmwareVersion,
                        const QString& errorMessage);

    void uploadProgress(const QString& deviceId, qint64 sent, qint64 total);

    // message is the response body on success (the robot's own "OK. Reset
    // the robot..." string) or a human-readable failure reason on failure --
    // reply->errorString(), or the body text for a 401/400/500 the robot
    // sent back deliberately (wrong password, empty body, no OTA partition).
    void uploadFinished(const QString& deviceId, bool success, const QString& message);

private:
    enum class PendingKind { Status, Upload };
    struct PendingResolution {
        PendingKind kind;
        QString deviceId;
        QString hostname;  // the original *.local name, for caching and for the OS-resolver fallback
        QString password;  // upload only
        QString filePath;  // upload only
    };

    QUrl buildUrl(const QString& address, const QString& path) const;
    // Empty deviceId means "resolution didn't have a matching pending
    // request" (stale/already handled) -- both onMdnsResolved() and
    // onMdnsResolveFailed() check this before acting.
    void sendStatusRequest(const QString& deviceId, const QString& host);
    void sendUploadRequest(const QString& deviceId, const QString& host, const QString& password,
                            const QString& filePath);
    QHostAddress cachedMdnsAddress(const QString& hostnameLower) const;
    void cacheMdnsAddress(const QString& hostnameLower, const QHostAddress& address);
    void onMdnsResolved(const QString& requestId, const QHostAddress& address);
    void onMdnsResolveFailed(const QString& requestId);

    QNetworkAccessManager* m_manager;
    MdnsResolver* m_mdnsResolver;
    QHash<QString, QNetworkReply*> m_statusReplies;  // keyed by deviceId
    QHash<QString, QNetworkReply*> m_uploadReplies;  // keyed by deviceId
    QHash<QString, PendingResolution> m_pendingResolutions;       // keyed by "status:"/"upload:" + deviceId
    QHash<QString, QPair<QHostAddress, qint64>> m_mdnsCache;      // hostname (lower) -> (address, expiry epoch ms)
};

}  // namespace traceview
