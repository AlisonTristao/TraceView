#include <QtTest/QtTest>

#include "updater/updatechecker.h"

using namespace traceview;

// UpdateChecker's own checkForUpdates() is a live network round trip against
// GitHub -- not something to exercise here (see test_otaclient.cpp's own
// comment on why "reachable over the real network" isn't testable offline).
// What IS worth pinning is parseReleasesResponse(): the parsing/comparison
// logic UpdateChecker's network callback hands off to, fed hand-built JSON
// the same way test_manifestclient.cpp feeds hand-built BTP bytes.

namespace {

class TestUpdateChecker : public QObject {
    Q_OBJECT

private slots:
    void detectsNewerRelease();
    void reportsUpToDateWhenNotNewer();
    void failsOnEmptyReleaseList();
    void failsOnUnparseableTag();
    void picksUpChecksumsAsset();
};

QByteArray releasesJson(const QString& tagName, bool prerelease = false) {
    return QStringLiteral(R"([{
        "tag_name": "%1",
        "prerelease": %2,
        "html_url": "https://github.com/AlisonTristao/TraceView/releases/tag/%1",
        "body": "Release notes",
        "assets": [
            {"name": "TraceView-9.9.9-windows-x64.exe",
             "browser_download_url": "https://example.invalid/win.exe", "size": 123},
            {"name": "TraceView-9.9.9-linux-x64.AppImage",
             "browser_download_url": "https://example.invalid/linux.AppImage", "size": 456},
            {"name": "TraceView-9.9.9-linux-arm64.AppImage",
             "browser_download_url": "https://example.invalid/arm.AppImage", "size": 789},
            {"name": "TraceView-9.9.9-linux-x64.tar.gz",
             "browser_download_url": "https://example.invalid/legacy.tar.gz", "size": 999},
            {"name": "SHA256SUMS.txt",
             "browser_download_url": "https://example.invalid/SHA256SUMS.txt", "size": 78}
        ]
    }])")
        .arg(tagName, prerelease ? QStringLiteral("true") : QStringLiteral("false"))
        .toUtf8();
}

void TestUpdateChecker::detectsNewerRelease() {
    const SemVer local{2, 4, 0};
    const auto result = parseReleasesResponse(releasesJson(QStringLiteral("v2.5.0")), local);
    QVERIFY(result.has_value());
    QVERIFY(result->hasUpdate);
    QCOMPARE(result->info.version, QStringLiteral("2.5.0"));
#if defined(Q_OS_LINUX)
    QCOMPARE(result->info.assetName, QStringLiteral("TraceView-9.9.9-linux-x64.AppImage"));
    QCOMPARE(result->info.assetSize, qint64(456));
#elif defined(Q_OS_WIN)
    QCOMPARE(result->info.assetName, QStringLiteral("TraceView-9.9.9-windows-x64.exe"));
#endif
    QVERIFY(result->info.releaseUrl.isValid());
    QVERIFY(result->info.checksumsUrl.isValid());
}

void TestUpdateChecker::reportsUpToDateWhenNotNewer() {
    const SemVer local{2, 5, 0};

    const auto sameVersion = parseReleasesResponse(releasesJson(QStringLiteral("v2.5.0")), local);
    QVERIFY(sameVersion.has_value());
    QVERIFY(!sameVersion->hasUpdate);

    const auto olderVersion = parseReleasesResponse(releasesJson(QStringLiteral("v2.4.0")), local);
    QVERIFY(olderVersion.has_value());
    QVERIFY(!olderVersion->hasUpdate);
}

void TestUpdateChecker::failsOnEmptyReleaseList() {
    QString reason;
    const auto result = parseReleasesResponse(QByteArrayLiteral("[]"), SemVer{2, 4, 0}, &reason);
    QVERIFY(!result.has_value());
    QVERIFY(!reason.isEmpty());
}

void TestUpdateChecker::failsOnUnparseableTag() {
    QString reason;
    const auto result = parseReleasesResponse(releasesJson(QStringLiteral("not-a-version")),
                                               SemVer{2, 4, 0}, &reason);
    QVERIFY(!result.has_value());
    QVERIFY(!reason.isEmpty());
}

void TestUpdateChecker::picksUpChecksumsAsset() {
    const auto result =
        parseReleasesResponse(releasesJson(QStringLiteral("v9.9.9")), SemVer{0, 0, 1});
    QVERIFY(result.has_value());
    QVERIFY(result->hasUpdate);
    QCOMPARE(result->info.checksumsUrl.toString(),
             QStringLiteral("https://example.invalid/SHA256SUMS.txt"));
}

}  // namespace

QTEST_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
