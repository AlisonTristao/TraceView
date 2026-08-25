#include "ota/otaclient.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "ota/mdnsresolver.h"

namespace traceview {

namespace {
// A status poll must never let one unreachable device stall the row's next
// poll cycle -- OtaTab's timer fires again on its own interval regardless,
// but a hung request would otherwise pile up alongside it. Uploads get no
// such timeout: a firmware binary can legitimately take longer than a few
// seconds and uploadProgress() already tells the user it's still moving.
constexpr int kStatusTimeoutMs = 4000;

// How long a resolved *.local address is trusted before the next request
// for it re-queries the network -- long enough that a poll every few
// seconds (OtaTab's timer) doesn't re-resolve on every tick, short enough
// that a robot picking up a new DHCP lease is noticed well within a normal
// working session.
constexpr qint64 kMdnsCacheTtlMs = 60000;
}  // namespace

OtaClient::OtaClient(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_mdnsResolver = new MdnsResolver(this);
    connect(m_mdnsResolver, &MdnsResolver::resolved, this, &OtaClient::onMdnsResolved);
    connect(m_mdnsResolver, &MdnsResolver::resolveFailed, this, &OtaClient::onMdnsResolveFailed);
}

QUrl OtaClient::buildUrl(const QString& address, const QString& path) const {
    QString host = address.trimmed();
    if (!host.contains(QStringLiteral("://"))) {
        host.prepend(QStringLiteral("http://"));
    }
    QUrl url(host);
    url.setPath(path);
    return url;
}

QHostAddress OtaClient::cachedMdnsAddress(const QString& hostnameLower) const {
    const auto it = m_mdnsCache.constFind(hostnameLower);
    if (it == m_mdnsCache.constEnd() || it->second <= QDateTime::currentMSecsSinceEpoch()) {
        return QHostAddress();
    }
    return it->first;
}

void OtaClient::cacheMdnsAddress(const QString& hostnameLower, const QHostAddress& address) {
    m_mdnsCache.insert(hostnameLower,
                       {address, QDateTime::currentMSecsSinceEpoch() + kMdnsCacheTtlMs});
}

bool OtaClient::statusRequestInFlight(const QString& deviceId) const {
    return m_statusReplies.contains(deviceId) ||
           m_pendingResolutions.contains(QStringLiteral("status:%1").arg(deviceId));
}

void OtaClient::checkStatus(const QString& deviceId, const QString& address) {
    // Coalesce rather than abort: see the class comment in the header. The
    // request already running will emit statusChecked() for this device, so
    // returning here still honours "one emission per poll that actually
    // started one".
    if (statusRequestInFlight(deviceId)) {
        return;
    }

    const QString host = address.trimmed();
    if (host.isEmpty()) {
        emit statusChecked(deviceId, /*reachable=*/false, /*otaReady=*/false, QString(),
                           tr("No OTA address configured for this device."));
        return;
    }

    if (!MdnsResolver::isMdnsHostname(host)) {
        sendStatusRequest(deviceId, host);
        return;
    }

    const QHostAddress cached = cachedMdnsAddress(host.toLower());
    if (!cached.isNull()) {
        sendStatusRequest(deviceId, cached.toString());
        return;
    }

    const QString requestId = QStringLiteral("status:%1").arg(deviceId);
    m_pendingResolutions.insert(requestId,
                                {PendingKind::Status, deviceId, host, QString(), QString()});
    m_mdnsResolver->resolve(requestId, host);
}

void OtaClient::sendStatusRequest(const QString& deviceId, const QString& host) {
    QNetworkRequest request(buildUrl(host, QStringLiteral("/status")));
    request.setTransferTimeout(kStatusTimeoutMs);

    QNetworkReply* reply = m_manager->get(request);
    m_statusReplies.insert(deviceId, reply);

    connect(reply, &QNetworkReply::finished, this, [this, deviceId, reply]() {
        // Defensive: checkStatus() coalesces instead of replacing, so a
        // second live reply for one device should not arise. It still can
        // if the device is removed and re-added under the same id mid-flight,
        // and a stale reply must not report anything back.
        if (m_statusReplies.value(deviceId) != reply) {
            reply->deleteLater();
            return;
        }
        m_statusReplies.remove(deviceId);

        if (reply->error() != QNetworkReply::NoError) {
            emit statusChecked(deviceId, /*reachable=*/false, /*otaReady=*/false, QString(),
                               reply->errorString());
            reply->deleteLater();
            return;
        }

        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        emit statusChecked(deviceId, /*reachable=*/true, body.value("ota_ready").toBool(true),
                           body.value("firmware").toString(), QString());
        reply->deleteLater();
    });
}

void OtaClient::uploadFirmware(const QString& deviceId, const QString& address,
                               const QString& password, const QString& filePath) {
    if (QNetworkReply* previous = m_uploadReplies.take(deviceId)) {
        previous->abort();
    }

    const QString host = address.trimmed();
    if (host.isEmpty()) {
        emit uploadFinished(deviceId, /*success=*/false,
                            tr("No OTA address configured for this device."));
        return;
    }

    if (!MdnsResolver::isMdnsHostname(host)) {
        sendUploadRequest(deviceId, host, password, filePath);
        return;
    }

    const QHostAddress cached = cachedMdnsAddress(host.toLower());
    if (!cached.isNull()) {
        sendUploadRequest(deviceId, cached.toString(), password, filePath);
        return;
    }

    const QString requestId = QStringLiteral("upload:%1").arg(deviceId);
    m_pendingResolutions.insert(requestId,
                                {PendingKind::Upload, deviceId, host, password, filePath});
    m_mdnsResolver->resolve(requestId, host);
}

void OtaClient::sendUploadRequest(const QString& deviceId, const QString& host,
                                  const QString& password, const QString& filePath) {
    auto* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        emit uploadFinished(deviceId, /*success=*/false,
                            tr("Couldn't open %1").arg(QFileInfo(filePath).fileName()));
        delete file;
        return;
    }

    QNetworkRequest request(buildUrl(host, QStringLiteral("/update")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/octet-stream"));
    request.setRawHeader("X-OTA-Password", password.toUtf8());

    QNetworkReply* reply = m_manager->post(request, file);
    file->setParent(reply);  // outlives the request, cleaned up with the reply
    m_uploadReplies.insert(deviceId, reply);

    connect(reply, &QNetworkReply::uploadProgress, this,
            [this, deviceId](qint64 sent, qint64 total) {
                emit uploadProgress(deviceId, sent, total);
            });

    connect(reply, &QNetworkReply::finished, this, [this, deviceId, reply]() {
        if (m_uploadReplies.value(deviceId) != reply) {
            reply->deleteLater();
            return;
        }
        m_uploadReplies.remove(deviceId);

        // The robot sends a readable body alongside both success (200, "OK.
        // Reset the robot...") and its own deliberate failures (401 wrong
        // password, 400 empty body, 500 no OTA partition/write failed) -- see
        // OTAUpdater.cpp's handle_update_post. A transport-level failure
        // (host unreachable, aborted) has no body, so fall back to
        // errorString() there.
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const bool success = reply->error() == QNetworkReply::NoError;
        const QString message = !body.isEmpty() ? body : reply->errorString();
        emit uploadFinished(deviceId, success, message);
        reply->deleteLater();
    });
}

void OtaClient::onMdnsResolved(const QString& requestId, const QHostAddress& address) {
    const PendingResolution pending = m_pendingResolutions.take(requestId);
    if (pending.deviceId.isEmpty())
        return;  // stale -- superseded or already handled

    cacheMdnsAddress(pending.hostname.toLower(), address);

    if (pending.kind == PendingKind::Status) {
        sendStatusRequest(pending.deviceId, address.toString());
    } else {
        sendUploadRequest(pending.deviceId, address.toString(), pending.password, pending.filePath);
    }
}

void OtaClient::onMdnsResolveFailed(const QString& requestId) {
    const PendingResolution pending = m_pendingResolutions.take(requestId);
    if (pending.deviceId.isEmpty())
        return;

    // Our own multicast query got no answer in time -- fall back to
    // whatever the OS resolver makes of the same hostname. Covers the case
    // where something like Bonjour IS installed but our raw probe still
    // missed (firewall rule specific to this socket, a network that filters
    // multicast, ...): QNetworkAccessManager will try getaddrinfo() on the
    // plain hostname exactly as before this resolver existed. If that also
    // fails, the normal QNetworkReply error path reports it the same way
    // any other unreachable host would be.
    if (pending.kind == PendingKind::Status) {
        sendStatusRequest(pending.deviceId, pending.hostname);
    } else {
        sendUploadRequest(pending.deviceId, pending.hostname, pending.password, pending.filePath);
    }
}

}  // namespace traceview
