#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "project/projectstore.h"

using traceview::ProjectStore;

namespace {

// ProjectStore::instance() is a process-wide singleton, so test order
// matters: saveWithoutPathFails() must run first (declaration order == run
// order for QTest) while currentPath() is still empty, before any other
// test calls saveAs()/load() and sets it.
class TestProjectStore : public QObject {
    Q_OBJECT

private slots:
    void saveWithoutPathFails();
    void saveAsWritesFormatVersionMeta();
    void saveDelegatesToCurrentPath();
    void loadRoundTripsSections();
    void loadInvalidJsonFailsWithoutCorruptingState();
};

void TestProjectStore::saveWithoutPathFails() {
    ProjectStore& store = ProjectStore::instance();
    QVERIFY(store.currentPath().isEmpty());
    QVERIFY(!store.save());
    QVERIFY(!store.lastError().isEmpty());
}

void TestProjectStore::saveAsWritesFormatVersionMeta() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("format_version.tvproj");

    ProjectStore& store = ProjectStore::instance();
    QVERIFY(store.saveAs(path));
    QCOMPARE(store.currentPath(), path);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value("traceview").toObject().value("formatVersion").toInt(), 1);
}

void TestProjectStore::saveDelegatesToCurrentPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("delegate.tvproj");

    ProjectStore& store = ProjectStore::instance();
    QVERIFY(store.saveAs(path));

    QJsonObject section;
    section["value"] = "updated";
    store.setSection("test_delegate", section);
    QVERIFY(store.save()); // no path argument -- must reuse `path` from saveAs()

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QCOMPARE(doc.object().value("test_delegate").toObject().value("value").toString(), QString("updated"));
}

void TestProjectStore::loadRoundTripsSections() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("roundtrip.tvproj");

    ProjectStore& store = ProjectStore::instance();

    QJsonObject original;
    original["foo"] = "bar";
    store.setSection("test_roundtrip", original);
    QVERIFY(store.saveAs(path));

    QJsonObject mutated;
    mutated["foo"] = "changed";
    store.setSection("test_roundtrip", mutated);
    QCOMPARE(store.section("test_roundtrip"), mutated);

    QVERIFY(store.load(path));
    QCOMPARE(store.currentPath(), path);
    QCOMPARE(store.section("test_roundtrip"), original);
}

void TestProjectStore::loadInvalidJsonFailsWithoutCorruptingState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString goodPath = dir.filePath("good.tvproj");
    const QString badPath = dir.filePath("bad.tvproj");

    ProjectStore& store = ProjectStore::instance();
    QJsonObject section;
    section["ok"] = true;
    store.setSection("test_invalid_json", section);
    QVERIFY(store.saveAs(goodPath));

    QFile bad(badPath);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{ not valid json");
    bad.close();

    QVERIFY(!store.load(badPath));
    QVERIFY(!store.lastError().isEmpty());
    // A failed load() must not clobber the previously-loaded state.
    QCOMPARE(store.currentPath(), goodPath);
    QCOMPARE(store.section("test_invalid_json"), section);
}

} // namespace

QTEST_MAIN(TestProjectStore)
#include "test_projectstore.moc"
