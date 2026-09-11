#include <QtTest/QtTest>

#include "updater/semver.h"

using namespace traceview;

namespace {

class TestSemVer : public QObject {
    Q_OBJECT

private slots:
    void parsesPlainVersion();
    void parsesVPrefixedTag();
    void ignoresPrereleaseSuffix();
    void rejectsGarbage();
    void comparesByPrecedence();
};

void TestSemVer::parsesPlainVersion() {
    const auto version = parseSemVer(QStringLiteral("2.4.0"));
    QVERIFY(version.has_value());
    QCOMPARE(version->major, 2);
    QCOMPARE(version->minor, 4);
    QCOMPARE(version->patch, 0);
}

void TestSemVer::parsesVPrefixedTag() {
    const auto version = parseSemVer(QStringLiteral("v2.10.3"));
    QVERIFY(version.has_value());
    QCOMPARE(version->major, 2);
    QCOMPARE(version->minor, 10);
    QCOMPARE(version->patch, 3);
}

void TestSemVer::ignoresPrereleaseSuffix() {
    // GitHub release tags aren't guaranteed to be a clean x.y.z -- a version
    // this feature might itself publish, "v2.5.0-beta.1", still has to
    // compare as 2.5.0 against the running build.
    const auto version = parseSemVer(QStringLiteral("v2.5.0-beta.1"));
    QVERIFY(version.has_value());
    QVERIFY(*version == (SemVer{2, 5, 0}));
}

void TestSemVer::rejectsGarbage() {
    QVERIFY(!parseSemVer(QStringLiteral("not-a-version")).has_value());
    QVERIFY(!parseSemVer(QString()).has_value());
}

void TestSemVer::comparesByPrecedence() {
    const SemVer v240{2, 4, 0};
    const SemVer v241{2, 4, 1};
    const SemVer v250{2, 5, 0};
    const SemVer v100{1, 9, 9};
    const SemVer v200{2, 0, 0};
    QVERIFY(v240 < v241);
    QVERIFY(v241 < v250);
    QVERIFY(v100 < v200);
    QVERIFY(!(v240 < v240));
}

}  // namespace

QTEST_MAIN(TestSemVer)
#include "test_semver.moc"
