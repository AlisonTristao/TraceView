#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include "updater/updateinstaller.h"

using namespace traceview;

class TestUpdateInstaller : public QObject {
    Q_OBJECT
private slots:
    void rejectsOutsideAppImage() {
        qunsetenv("APPIMAGE");
        QString reason;
        QVERIFY(!UpdateInstaller::install(QStringLiteral("missing.AppImage"), &reason));
        QVERIFY(!reason.isEmpty());
    }

    void rejectsInvalidDownloadWithoutChangingImage() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile old(dir.filePath("old.AppImage"));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("original");
        old.close();
        QFile download(dir.filePath("download"));
        QVERIFY(download.open(QIODevice::WriteOnly));
        download.write("not an AppImage");
        download.close();
        qputenv("APPIMAGE", old.fileName().toUtf8());
        QString reason;
        QVERIFY(!UpdateInstaller::install(download.fileName(), &reason));
        QVERIFY(!reason.isEmpty());
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("original"));
        qunsetenv("APPIMAGE");
    }

    void replacesAndRelaunches() {
        QTemporaryDir dir(QDir::tempPath() + "/traceview space ' quote.XXXXXX");
        QVERIFY(dir.isValid());
        const QString target = dir.filePath("TraceView ' $image.AppImage");
        const QString download = dir.filePath("download.AppImage");
        const QString marker = dir.filePath("restarted");
        QFile old(target);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("original");
        old.close();
        // ELF padding can carry the type-2 AppImage signature. This executable
        // serves as the new image so the test exercises an actual exec/relaunch.
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), download));
        QFile image(download);
        QVERIFY(image.open(QIODevice::ReadWrite));
        QVERIFY(image.seek(8));
        QCOMPARE(image.write(QByteArray::fromHex("414902")), qint64(3));
        image.close();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("APPIMAGE", target);
        environment.insert("TRACEVIEW_TEST_RESTART_MARKER", marker);
        QProcess child;
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--apply", download});
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 10000);
        QVERIFY(QFileInfo(target).isExecutable());
        QVERIFY(old.open(QIODevice::ReadOnly));
        QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), image.readAll());
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--apply")) {
        QString reason;
        if (!UpdateInstaller::install(app.arguments().last(), &reason)) {
            qWarning() << reason;
            return 1;
        }
        return 0;
    }
    const QString marker = qEnvironmentVariable("TRACEVIEW_TEST_RESTART_MARKER");
    if (!marker.isEmpty()) {
        if (!qEnvironmentVariableIsEmpty("APPIMAGE")) return 2;
        QFile file(marker);
        return file.open(QIODevice::WriteOnly) ? 0 : 3;
    }
    TestUpdateInstaller test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_updateinstaller.moc"
