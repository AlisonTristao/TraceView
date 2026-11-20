#include "ota/otaclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "devices/device.h"
#include "ota/mdnsresolver.h"
#include "ota/otaauth.h"

namespace traceview {

namespace {
// A status poll must never let one unreachable device stall the row's next
// poll cycle. Uploads get no such timeout: an image legitimately takes longer
// than a few seconds and uploadProgress() already shows it is still moving.
constexpr int kStatusTimeoutMs = 4000;

// How long a resolved *.local address is trusted before the next request for
// it re-queries the network -- long enough that a poll every second doesn't
// re-resolve on every tick, short enough that a new DHCP lease is noticed.
constexpr qint64 kMdnsCacheTtlMs = 60000;

RecoveryStatus parseStatus(const QJsonObject& body) {
    RecoveryStatus status;
    status.name = body.value("name").toString();
    status.chip = body.value("chip").toString();
    status.recoveryVersion = body.value("recovery").toString();
    status.reason = body.value("reason").toString();
    const QJsonObject app = body.value("app").toObject();
    status.appValid = app.value("valid").toBool();
    status.appProject = app.value("project").toString();
    status.appVersion = app.value("version").toString();
    status.otaReady = body.value("ota_ready").toBool();
    status.authRequired = body.value("auth").toString() != QLatin1String("none");
    status.maxSize = static_cast<qint64>(body.value("max_size").toDouble());
    status.nonce = QByteArray::fromHex(body.value("nonce").toString().toLatin1());
    status.ownNetwork = body.value("ap").toString();
    return status;
}

// The reply text when the recovery sent one, else Qt's transport error.
QString replyMessage(QNetworkReply* reply) {
    const QString body = QString::fromUtf8(reply->readAll()).trimmed();
    if (!body.isEmpty()) {
        return body;
    }
    return reply->errorString();
}
}  // namespace

QString otaAddressFor(const Device& device) {
    const QString own = device.otaAddress.trimmed();
    if (!own.isEmpty()) {
        return own;
    }
    for (const DeviceInfoRecord& entry : device.reportedInfo) {
        if (entry.key == QLatin1String("ota_endpoint") && !entry.value.trimmed().isEmpty()) {
            return entry.value.trimmed();
        }
    }
    if (device.transportType == TransportType::Tcp) {
        return device.tcpHost.trimmed();
    }
    return QString();
}

OtaClient::OtaClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<traceview::RecoveryStatus>();
    m_manager = new QNetworkAccessManager(this);
    m_mdnsResolver = new MdnsResolver(this);
    connect(m_mdnsResolver, &MdnsResolver::resolved, this, &OtaClient::onMdnsResolved);
    connect(m_mdnsResolver, &MdnsResolver::resolveFailed, this, &OtaClient::onMdnsResolveFailed);
}

QUrl OtaClient::buildUrl(const QString& host, const QString& path) const {
    QString base = host.trimmed();
    if (!base.contains(QStringLiteral("://"))) {
        base.prepend(QStringLiteral("http://"));
    }
    QUrl url(base);
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

void OtaClient::withHost(const QString& requestId, const QString& address, HostContinuation then) {
    const QString host = address.trimmed();
    if (!MdnsResolver::isMdnsHostname(host)) {
        then(host);
        return;
    }
    const QHostAddress cached = cachedMdnsAddress(host.toLower());
    if (!cached.isNull()) {
        then(cached.toString());
        return;
    }
    m_pendingResolutions.insert(requestId, {host, std::move(then)});
    m_mdnsResolver->resolve(requestId, host);
}

void OtaClient::onMdnsResolved(const QString& requestId, const QHostAddress& address) {
    const PendingResolution pending = m_pendingResolutions.take(requestId);
    if (!pending.then) {
        return;  // stale -- superseded or already handled
    }
    cacheMdnsAddress(pending.hostname.toLower(), address);
    pending.then(address.toString());
}

void OtaClient::onMdnsResolveFailed(const QString& requestId) {
    const PendingResolution pending = m_pendingResolutions.take(requestId);
    if (!pending.then) {
        return;
    }
    // Our own multicast query got no answer in time -- let the OS resolver
    // try the same name (Bonjour may be installed, or the network filters our
    // probe). If that fails too, the request reports it like any other
    // unreachable host.
    pending.then(pending.hostname);
}

QNetworkReply* OtaClient::fetchStatus(const QString& host, StatusContinuation then) {
    QNetworkRequest request(buildUrl(host, QStringLiteral("/status")));
    request.setTransferTimeout(kStatusTimeoutMs);
    QNetworkReply* reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, then = std::move(then)]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            then(false, RecoveryStatus(), reply->errorString());
            return;
        }
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        if (body.value("mode").toString() != QLatin1String("recovery")) {
            then(false, RecoveryStatus(),
                 tr("Something answered at this address, but not a TraceKit recovery."));
            return;
        }
        then(true, parseStatus(body), QString());
    });
    return reply;
}

bool OtaClient::statusRequestInFlight(const QString& deviceId) const {
    return m_statusReplies.contains(deviceId) ||
           m_pendingResolutions.contains(QStringLiteral("status:%1").arg(deviceId));
}

void OtaClient::checkStatus(const QString& deviceId, const QString& address) {
    // Coalesce rather than abort: see the class comment.
    if (statusRequestInFlight(deviceId)) {
        return;
    }
    if (address.trimmed().isEmpty()) {
        emit statusChecked(deviceId, /*reachable=*/false, RecoveryStatus(),
                           tr("No address for this device: connect it over TCP, or set its "
                              "OTA address in Device Settings."));
        return;
    }
    withHost(QStringLiteral("status:%1").arg(deviceId), address, [this, deviceId](const QString& host) {
        QNetworkReply* reply = fetchStatus(
            host, [this, deviceId](bool ok, const RecoveryStatus& status, const QString& error) {
                m_statusReplies.remove(deviceId);
                emit statusChecked(deviceId, ok, status, error);
            });
        m_statusReplies.insert(deviceId, reply);
    });
}

void OtaClient::uploadFirmware(const QString& deviceId, const QString& address,
                               const QByteArray& keyE, const QString& filePath) {
    if (QNetworkReply* previous = m_uploadReplies.take(deviceId)) {
        previous->abort();
    }
    if (address.trimmed().isEmpty()) {
        emit uploadFinished(deviceId, false, tr("No address for this device."));
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit uploadFinished(deviceId, false,
                            tr("Couldn't open %1").arg(QFileInfo(filePath).fileName()));
        return;
    }
    const QByteArray image = file.readAll();
    if (image.isEmpty() || static_cast<unsigned char>(image.at(0)) != kImageMagic) {
        emit uploadFinished(deviceId, false,
                            tr("%1 is not an ESP application image (use the firmware.bin of "
                               "the build, not the merged or bootloader one).")
                                .arg(QFileInfo(filePath).fileName()));
        return;
    }

    withHost(QStringLiteral("upload:%1").arg(deviceId), address,
             [this, deviceId, keyE, image](const QString& host) {
                 sendUpload(deviceId, host, keyE, image);
             });
}

void OtaClient::sendUpload(const QString& deviceId, const QString& host, const QByteArray& keyE,
                           const QByteArray& image) {
    // A fresh nonce first: the recovery changes it after every signed request.
    QNetworkReply* statusReply = fetchStatus(
        host, [this, deviceId, host, keyE, image](bool ok, const RecoveryStatus& status,
                                                  const QString& error) {
            m_uploadReplies.remove(deviceId);
            if (!ok) {
                emit uploadFinished(deviceId, false, error);
                return;
            }
            if (!status.otaReady) {
                emit uploadFinished(deviceId, false,
                                    tr("The recovery is already writing another upload."));
                return;
            }
            if (status.maxSize > 0 && image.size() > status.maxSize) {
                emit uploadFinished(deviceId, false,
                                    tr("The image has %1 KB; the app partition holds %2 KB.")
                                        .arg(image.size() / 1024)
                                        .arg(status.maxSize / 1024));
                return;
            }

            const QByteArray digest = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
            QNetworkRequest request(buildUrl(host, QStringLiteral("/update")));
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/octet-stream"));
            request.setRawHeader("X-OTA-SHA256", digest.toHex());
            if (status.authRequired) {
                const QByteArray mac = ota::authMac(keyE, status.nonce, ota::kActionUpdate, digest);
                if (mac.isEmpty()) {
                    emit uploadFinished(deviceId, false,
                                        tr("No device password to sign the upload with."));
                    return;
                }
                request.setRawHeader("X-OTA-Auth", mac);
            }

            QNetworkReply* reply = m_manager->post(request, image);
            m_uploadReplies.insert(deviceId, reply);
            connect(reply, &QNetworkReply::uploadProgress, this,
                    [this, deviceId](qint64 sent, qint64 total) {
                        emit uploadProgress(deviceId, sent, total);
                    });
            connect(reply, &QNetworkReply::finished, this, [this, deviceId, reply]() {
                reply->deleteLater();
                if (m_uploadReplies.value(deviceId) != reply) {
                    return;  // superseded by a newer upload for this device
                }
                m_uploadReplies.remove(deviceId);
                const int code =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString message = replyMessage(reply);
                if (code == 401) {
                    message = tr("The device refused the signature: wrong password "
                                 "(the device's password E). %1")
                                  .arg(message);
                }
                emit uploadFinished(deviceId, reply->error() == QNetworkReply::NoError, message);
            });
        });
    m_uploadReplies.insert(deviceId, statusReply);
}

void OtaClient::rebootToApp(const QString& deviceId, const QString& address,
                            const QByteArray& keyE) {
    if (address.trimmed().isEmpty()) {
        emit rebootFinished(deviceId, false, tr("No address for this device."));
        return;
    }
    withHost(QStringLiteral("reboot:%1").arg(deviceId), address,
             [this, deviceId, keyE](const QString& host) { sendReboot(deviceId, host, keyE); });
}

void OtaClient::sendReboot(const QString& deviceId, const QString& host, const QByteArray& keyE) {
    fetchStatus(host, [this, deviceId, host, keyE](bool ok, const RecoveryStatus& status,
                                                   const QString& error) {
        if (!ok) {
            emit rebootFinished(deviceId, false, error);
            return;
        }
        QNetworkRequest request(buildUrl(host, QStringLiteral("/reboot")));
        request.setTransferTimeout(kStatusTimeoutMs);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/octet-stream"));
        if (status.authRequired) {
            const QByteArray mac =
                ota::authMac(keyE, status.nonce, ota::kActionReboot, QByteArray(32, '\0'));
            if (mac.isEmpty()) {
                emit rebootFinished(deviceId, false, tr("No device password to sign with."));
                return;
            }
            request.setRawHeader("X-OTA-Auth", mac);
        }
        QNetworkReply* reply = m_manager->post(request, QByteArray());
        connect(reply, &QNetworkReply::finished, this, [this, deviceId, reply]() {
            reply->deleteLater();
            emit rebootFinished(deviceId, reply->error() == QNetworkReply::NoError,
                                replyMessage(reply));
        });
    });
}

}  // namespace traceview
