#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "crash/sessionmarker.h"

using namespace traceview;

namespace {

class TestSessionMarker : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanRunIsNotACrash();
    void missingEndIsACrash();
    void updateQuitIsNotACrash();
    void endTwiceIsSafe();
};

void TestSessionMarker::initTestCase() {
    QCoreApplication::setApplicationVersion(QStringLiteral("4.19.0"));
}

void TestSessionMarker::cleanRunIsNotACrash() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionMarker first(dir.path());
    first.begin();
    QVERIFY(QFile::exists(first.markerFilePath()));
    first.end();
    QVERIFY(!QFile::exists(first.markerFilePath()));

    SessionMarker second(dir.path());
    second.begin();
    QVERIFY(!second.previousSessionCrashed());
    second.end();
}

void TestSessionMarker::missingEndIsACrash() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionMarker first(dir.path());
    first.begin();

    SessionMarker second(dir.path());
    second.begin();
    QVERIFY(second.previousSessionCrashed());
    QCOMPARE(second.previousSession().version, first.currentSession().version);
    QCOMPARE(second.previousSession().logFilePath, first.currentSession().logFilePath);
    QCOMPARE(second.previousSession().crashDir, first.currentSession().crashDir);
    second.end();
}

void TestSessionMarker::updateQuitIsNotACrash() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionMarker first(dir.path());
    first.begin();
    first.markUpdating(QStringLiteral("4.18.0"));
    first.end();

    SessionMarker second(dir.path());
    second.begin();
    QVERIFY(!second.previousSessionCrashed());
    QCOMPARE(second.previousSession().updatedFrom, QStringLiteral("4.18.0"));
    second.end();

    SessionMarker third(dir.path());
    third.begin();
    QVERIFY(third.previousSession().updatedFrom.isEmpty());
    third.end();
}

void TestSessionMarker::endTwiceIsSafe() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionMarker marker(dir.path());
    marker.begin();
    marker.end();
    marker.end();
    QVERIFY(!QFile::exists(marker.markerFilePath()));
}

}  // namespace

QTEST_MAIN(TestSessionMarker)
#include "test_sessionmarker.moc"
