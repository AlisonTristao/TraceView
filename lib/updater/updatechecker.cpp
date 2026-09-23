#include "updater/updatechecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "traceview/version.h"

namespace traceview {

namespace {
constexpr char kReleasesUrl[] = "https://api.github.com/repos/AlisonTristao/TraceView/releases";
constexpr char kChecksumsAssetName[] = "SHA256SUMS.txt";

// Matches CPACK_PACKAGE_FILE_NAME's per-platform suffix (see CMakeLists.txt):
// "...-windows-x64.exe" / "...-linux-x64.AppImage" / "...-android-arm64.apk" /
// "...-macos-arm64.dmg" (scripts/build_macos_dmg.sh).
// Android is checked before Linux because Q_OS_LINUX is defined there too. Empty on any other
// platform (iOS included: an iOS app may not download and install itself) -- there is no
// packaged build to match there, so the asset lookup below just never finds one.
#if defined(TRACEVIEW_FLATPAK_BUILD)
constexpr char kAssetSuffix[] = "";
#elif defined(Q_OS_ANDROID)
constexpr char kAssetSuffix[] = "-android-arm64.apk";
#elif defined(Q_OS_WIN)
constexpr char kAssetSuffix[] = ".exe";
#elif defined(Q_OS_MACOS)
constexpr char kAssetSuffix[] = "-macos-arm64.dmg";
#elif defined(Q_OS_LINUX)
constexpr char kAssetSuffix[] = "-linux-x64.AppImage";
#else
constexpr char kAssetSuffix[] = "";
#endif
}  // namespace

std::optional<CheckResult> parseReleasesResponse(const QByteArray& body,
                                                  const SemVer& localVersion,
                                                  QString* errorReason) {
    const auto fail = [errorReason](const QString& reason) -> std::optional<CheckResult> {
        if (errorReason) {
            *errorReason = reason;
        }
        return std::nullopt;
    };

    const QJsonArray releases = QJsonDocument::fromJson(body).array();
    if (releases.isEmpty()) {
        return fail(QObject::tr("No published releases found."));
    }

    const QJsonObject latest = releases.first().toObject();
    const std::optional<SemVer> remote = parseSemVer(latest.value("tag_name").toString());
    if (!remote) {
        return fail(QObject::tr("Couldn't parse the latest release's version."));
    }

    CheckResult result;
    result.hasUpdate = localVersion < *remote;
    if (!result.hasUpdate) {
        return result;
    }

    UpdateInfo& info = result.info;
    info.version = QString("%1.%2.%3").arg(remote->major).arg(remote->minor).arg(remote->patch);
    info.releaseNotes = latest.value("body").toString();
    info.releaseUrl = QUrl(latest.value("html_url").toString());
    info.prerelease = latest.value("prerelease").toBool();

    const QJsonArray assets = latest.value("assets").toArray();
    for (const QJsonValue& assetValue : assets) {
        const QJsonObject asset = assetValue.toObject();
        const QString name = asset.value("name").toString();
        const QUrl url(asset.value("browser_download_url").toString());
        if (kAssetSuffix[0] != '\0' &&
            name.endsWith(QString::fromLatin1(kAssetSuffix), Qt::CaseInsensitive)) {
            info.assetName = name;
            info.assetUrl = url;
            info.assetSize = asset.value("size").toVariant().toLongLong();
        } else if (name.compare(QString::fromLatin1(kChecksumsAssetName), Qt::CaseInsensitive) ==
                   0) {
            info.checksumsUrl = url;
        }
    }

    return result;
}

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
}

bool UpdateChecker::checkInFlight() const {
    return m_reply != nullptr;
}

void UpdateChecker::checkForUpdates() {
#if defined(TRACEVIEW_FLATPAK_BUILD)
    emit checkFailed(tr("Updates are managed by Flatpak. Use 'flatpak update'."));
    return;
#endif
    if (m_reply != nullptr) {
        return;
    }

    QNetworkRequest request{QUrl(QString::fromLatin1(kReleasesUrl))};
    // GitHub's REST API answers 403 to requests with no User-Agent at all,
    // regardless of rate limit -- anything identifying satisfies it.
    request.setRawHeader("User-Agent", "TraceView-UpdateChecker");
    request.setRawHeader("Accept", "application/vnd.github+json");

    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }

        const SemVer local{kVersionMajor, kVersionMinor, kVersionPatch};
        QString reason;
        const std::optional<CheckResult> result =
            parseReleasesResponse(reply->readAll(), local, &reason);
        if (!result) {
            emit checkFailed(reason);
            return;
        }

        if (result->hasUpdate) {
            emit updateAvailable(result->info);
        } else {
            emit upToDate();
        }
    });
}

}  // namespace traceview
