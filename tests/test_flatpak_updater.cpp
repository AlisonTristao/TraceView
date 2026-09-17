#include <QtTest>
#include <QSignalSpy>

#include "updater/updatechecker.h"
#include "updater/updatedownloader.h"
#include "updater/updateinstaller.h"

using namespace traceview;

class TestFlatpakUpdater : public QObject {
    Q_OBJECT
private slots:
    void ignoresAppImageAssets() {
        const auto result = parseReleasesResponse(
            R"([{"tag_name":"v99.0.0","assets":[{"name":"TraceView-99.0.0-linux-x64.AppImage","browser_download_url":"https://example.org/app.AppImage"}]}])",
            SemVer{1, 0, 0}, nullptr);
        QVERIFY(result.has_value());
        QVERIFY(result->info.assetUrl.isEmpty());
        QVERIFY(result->info.assetName.isEmpty());
    }

    void refusesGitHubCheck() {
        UpdateChecker checker;
        QSignalSpy failed(&checker, &UpdateChecker::checkFailed);
        checker.checkForUpdates();
        QCOMPARE(failed.count(), 1);
        QVERIFY(!checker.checkInFlight());
        QVERIFY(failed.first().first().toString().contains("Flatpak"));
    }

    void refusesDownload() {
        UpdateDownloader downloader;
        QSignalSpy failed(&downloader, &UpdateDownloader::failed);
        downloader.download(QUrl("http://127.0.0.1:1/app"), "app.AppImage",
                            QUrl("http://127.0.0.1:1/sums"));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("Flatpak"));
    }

    void refusesInstall() {
        QString reason;
        QVERIFY(!UpdateInstaller::install("unused.AppImage", &reason));
        QVERIFY(reason.contains("Flatpak"));
    }
};

QTEST_GUILESS_MAIN(TestFlatpakUpdater)
#include "test_flatpak_updater.moc"
