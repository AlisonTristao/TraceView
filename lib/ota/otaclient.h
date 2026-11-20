#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace traceview {

class MdnsResolver;
struct Device;

// What a TraceKit recovery says about itself in GET /status (TraceKit
// recovery/main/main.cpp, handle_status). Only meaningful when the request
// reached a recovery -- see OtaClient::statusChecked.
struct RecoveryStatus {
    QString name;              // the device's name (also its <name>.local)
    QString chip;              // "esp32s3", ...
    QString recoveryVersion;   // the recovery firmware's own version
    QString reason;            // why it is in the recovery: "requested", "crash_loop", ...
    bool appValid = false;     // a bootable application is in the app partition
    QString appProject;        // that application's project name and version
    QString appVersion;
    bool otaReady = false;     // false while an upload is already being written
    bool authRequired = true;  // "hmac-sha256"; false only on a DEVELOPMENT recovery
    qint64 maxSize = 0;        // the app partition's size: the largest image accepted
    QByteArray nonce;          // 16 octets, for the next signed request
    QString ownNetwork;        // the recovery's own Wi-Fi ("btp-<name>"), when it raised one
};

// The address the OTA tab talks to for `device`: its own otaAddress when the
// user set one, else the endpoint the device announced in its manifest
// (source_info "ota_endpoint", which TraceKit fills in), else the TCP host it
// connects through. Empty when none of those exists.
QString otaAddressFor(const Device& device);

// HTTP client for the TraceKit recovery -- the firmware a TraceKit device
// restarts into on `btp -recovery`, a crash loop, or a failed update: GET
// /status to poll it, POST /update to send an application image and POST
// /reboot to go back to the application. Both POSTs are signed with key E
// (ota/otaauth.h) over a nonce fetched right before them, so each upload or
// reboot costs one extra GET.
//
// Requests are keyed by Device::id. A second checkStatus() for a device that
// already has one outstanding is COALESCED into it -- it returns without
// starting a second request, and the one already running reports for both.
// Coalescing rather than aborting-and-restarting, because OtaTab polls every
// second while a status request is allowed four (kStatusTimeoutMs): aborting
// meant any device answering slower than the poll interval had every attempt
// cancelled by the next tick and never resolved once.
//
// A *.local address is resolved through MdnsResolver (our own multicast
// query, see its header for why) before QNetworkAccessManager sees it -- Qt
// would otherwise hand it to the OS resolver, which is unreliable on Windows.
// Resolved addresses are cached briefly (kMdnsCacheTtlMs).
class OtaClient : public QObject {
    Q_OBJECT

public:
    explicit OtaClient(QObject* parent = nullptr);

    // GET http://<address>/status. Resolves to exactly one statusChecked()
    // for this deviceId, including on a resolution/network error or timeout
    // (reachable=false). A call made while this device already has a status
    // request in flight is coalesced into it.
    void checkStatus(const QString& deviceId, const QString& address);

    // True while `deviceId` has a status request (or the mDNS resolution
    // that precedes one) outstanding.
    bool statusRequestInFlight(const QString& deviceId) const;

    // Sends the application image at `filePath`: checks it is an ESP image
    // that fits the partition, fetches a fresh nonce, and POSTs it signed
    // with `keyE` (unused by a recovery that does not require auth). Emits
    // uploadProgress as it goes and exactly one uploadFinished() at the end.
    // On success the recovery restarts into the new application by itself.
    void uploadFirmware(const QString& deviceId, const QString& address, const QByteArray& keyE,
                        const QString& filePath);

    // POST /reboot: leave the recovery for the application already in the
    // app partition. Exactly one rebootFinished().
    void rebootToApp(const QString& deviceId, const QString& address, const QByteArray& keyE);

    // Every image this client sends must start with this byte (the ESP
    // application image magic) -- anything else is refused before upload.
    static constexpr unsigned char kImageMagic = 0xE9;

signals:
    // reachable is true only when the address answered as a TraceKit
    // recovery; `status` is filled then. Otherwise errorMessage says why --
    // "Host not found" (a name neither our mDNS query nor the OS resolved)
    // versus "Connection refused" (the host is up but nothing listens: the
    // application is running) versus a timeout. OtaTab shows it as a tooltip.
    void statusChecked(const QString& deviceId, bool reachable,
                       const traceview::RecoveryStatus& status, const QString& errorMessage);

    void uploadProgress(const QString& deviceId, qint64 sent, qint64 total);

    // message is the recovery's own reply ("OK. Rebooting into ..." on
    // success, the reason on a refusal: bad signature, image too large...)
    // or a human-readable failure found before or while sending.
    void uploadFinished(const QString& deviceId, bool success, const QString& message);
    void rebootFinished(const QString& deviceId, bool success, const QString& message);

private:
    using HostContinuation = std::function<void(const QString& host)>;
    using StatusContinuation =
        std::function<void(bool ok, const RecoveryStatus& status, const QString& error)>;

    QUrl buildUrl(const QString& host, const QString& path) const;
    // Runs `then` with a host QNetworkAccessManager can use: the address
    // itself, a cached/fresh mDNS answer, or -- when our query gets no
    // answer -- the plain name for the OS resolver to try.
    void withHost(const QString& requestId, const QString& address, HostContinuation then);
    // GET /status and parse it; `then` runs exactly once. `reply` (optional)
    // receives the request so the caller can track or abort it.
    QNetworkReply* fetchStatus(const QString& host, StatusContinuation then);
    void sendUpload(const QString& deviceId, const QString& host, const QByteArray& keyE,
                    const QByteArray& image);
    void sendReboot(const QString& deviceId, const QString& host, const QByteArray& keyE);
    QHostAddress cachedMdnsAddress(const QString& hostnameLower) const;
    void cacheMdnsAddress(const QString& hostnameLower, const QHostAddress& address);
    void onMdnsResolved(const QString& requestId, const QHostAddress& address);
    void onMdnsResolveFailed(const QString& requestId);

    struct PendingResolution {
        QString hostname;  // the original *.local name, for caching and the OS fallback
        HostContinuation then;
    };

    QNetworkAccessManager* m_manager;
    MdnsResolver* m_mdnsResolver;
    QHash<QString, QNetworkReply*> m_statusReplies;  // keyed by deviceId
    QHash<QString, QNetworkReply*> m_uploadReplies;  // keyed by deviceId
    QHash<QString, PendingResolution> m_pendingResolutions;  // "status:"/"upload:"/"reboot:" + id
    QHash<QString, QPair<QHostAddress, qint64>> m_mdnsCache;  // hostname (lower) -> (address, expiry)
};

}  // namespace traceview

Q_DECLARE_METATYPE(traceview::RecoveryStatus)
