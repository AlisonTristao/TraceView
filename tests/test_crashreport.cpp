#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "crash/crashhandler.h"
#include "crash/crashreport.h"

using namespace traceview;

namespace {

bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

// A fake crashed run: 500-line log plus a crash dir holding stack.txt.
struct Fixture {
    QTemporaryDir dir;
    SessionInfo info;

    Fixture() {
        QByteArray log;
        for (int i = 1; i <= 500; ++i) {
            log += "line " + QByteArray::number(i) + "\n";
        }
        info.logFilePath = dir.filePath(QStringLiteral("app.log"));
        writeFile(info.logFilePath, log);
        info.crashDir = dir.filePath(QStringLiteral("crash"));
        QDir().mkpath(info.crashDir);
        writeFile(QDir(info.crashDir).filePath(CrashHandler::stackFileName()),
                  "frame0\nframe1\n");
        info.version = QStringLiteral("1.2.3");
        info.startedAt = QDateTime(QDate(2026, 11, 25), QTime(10, 0, 0));
    }
};

class TestCrashReport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void crashedSessionLoadsTailAndStack();
    void textHasTheSections();
    void saveCreatesFiles();
    void missingLogGivesEmptyTail();
    void issueUrlEncodesVersion();
};

void TestCrashReport::initTestCase() {
    QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7"));
}

void TestCrashReport::crashedSessionLoadsTailAndStack() {
    Fixture f;
    const CrashReport r = CrashReport::fromCrashedSession(f.info);
    QVERIFY(r.crashed);
    QCOMPARE(r.appVersion, QStringLiteral("1.2.3"));
    QCOMPARE(r.logTail.size(), CrashReport::kLogTailLines);
    QCOMPARE(r.logTail.last(), QStringLiteral("line 500"));
    QCOMPARE(r.logTail.first(), QStringLiteral("line 301"));
    QVERIFY(r.stackTrace.contains(QStringLiteral("frame1")));
    QVERIFY(r.dumpFilePath.isEmpty());
}

void TestCrashReport::textHasTheSections() {
    Fixture f;
    CrashReport r = CrashReport::fromCrashedSession(f.info);
    r.userDescription = QStringLiteral("opened a workspace");
    const QString text = r.toText();
    QVERIFY(text.contains(QStringLiteral("1.2.3")));
    QVERIFY(text.contains(QStringLiteral("Stack trace")));
    QVERIFY(text.contains(QStringLiteral("opened a workspace")));
    QVERIFY(text.contains(QStringLiteral("line 500")));
}

void TestCrashReport::saveCreatesFiles() {
    Fixture f;
    const CrashReport r = CrashReport::fromCrashedSession(f.info);
    QTemporaryDir out;
    QString error;
    const QString folder = r.saveToFolder(out.path(), &error);
    QVERIFY2(!folder.isEmpty(), qPrintable(error));
    QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("report.txt"))));
    QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("traceview.log"))));
}

void TestCrashReport::missingLogGivesEmptyTail() {
    QVERIFY(CrashReport::readTail(QStringLiteral("/no/such/file.log"), 10).isEmpty());
}

void TestCrashReport::issueUrlEncodesVersion() {
    CrashReport r;
    r.crashed = true;
    r.appVersion = QStringLiteral("1.2.3");
    QVERIFY(r.issueUrl().contains(QStringLiteral("Crash%20in%20v1.2.3")));
    QVERIFY(r.issueUrl().endsWith(QStringLiteral("&labels=bug")));
    r.crashed = false;
    QVERIFY(r.issueUrl().contains(QStringLiteral("Bug%20report%20v1.2.3")));
}

}  // namespace

QTEST_GUILESS_MAIN(TestCrashReport)
#include "test_crashreport.moc"
