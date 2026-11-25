#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "project/dashboardgallery.h"
#include "project/projectstore.h"

using traceview::DashboardGallery;
using traceview::GalleryEntryInfo;

namespace {

QJsonObject workspace(const QString& icon, int items) {
    QJsonArray array;
    for (int i = 0; i < items; ++i) {
        array.append(QJsonObject{{"type", "chart"}});
    }
    QJsonObject result{{"id", "w"}, {"dashboard", QJsonObject{{"items", array}}}};
    if (!icon.isEmpty()) {
        result["icon"] = icon;
    }
    return result;
}

QString writeProject(const QTemporaryDir& dir, const QString& name, const QJsonObject& root) {
    const QString path = dir.filePath(name);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return path;
}

QJsonObject readProject(const QString& path) {
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(file.readAll()).object();
}

class TestDashboardGalleryIcon : public QObject {
    Q_OBJECT

private slots:
    void infoCountsAndFallsBackToWorkspaceIcon();
    void infoPrefersTheGalleryIcon();
    void infoOfUnreadableFileHasOnlyTheName();
    void setIconRoundTripsAndClears();
    void setIconRefusesBuiltInExample();
};

void TestDashboardGalleryIcon::infoCountsAndFallsBackToWorkspaceIcon() {
    QTemporaryDir dir;
    const QJsonArray workspaces{workspace("lucide:rocket", 2), workspace("lucide:cog", 3)};
    const QString path = writeProject(
        dir, "demo.tvproj", {{"workspaces", QJsonObject{{"workspaces", workspaces}}}});
    const GalleryEntryInfo info = DashboardGallery::infoForPath(path);
    QCOMPARE(info.name, QStringLiteral("demo"));
    QCOMPARE(info.workspaces, 2);
    QCOMPARE(info.widgets, 5);
    QCOMPARE(info.icon, QStringLiteral("lucide:rocket"));
    QVERIFY(!info.iconIsCustom);
}

void TestDashboardGalleryIcon::infoPrefersTheGalleryIcon() {
    QTemporaryDir dir;
    const QString path = writeProject(
        dir, "demo.tvproj",
        {{"gallery", QJsonObject{{"icon", "lucide:star"}}},
         {"workspaces", QJsonObject{{"workspaces", QJsonArray{workspace("lucide:cog", 1)}}}}});
    const GalleryEntryInfo info = DashboardGallery::infoForPath(path);
    QCOMPARE(info.icon, QStringLiteral("lucide:star"));
    QVERIFY(info.iconIsCustom);
}

void TestDashboardGalleryIcon::infoOfUnreadableFileHasOnlyTheName() {
    QTemporaryDir dir;
    const GalleryEntryInfo missing = DashboardGallery::infoForPath(dir.filePath("nope.tvproj"));
    QCOMPARE(missing.name, QStringLiteral("nope"));
    QCOMPARE(missing.workspaces, 0);
    QVERIFY(missing.icon.isEmpty());

    const QString bad = dir.filePath("bad.tvproj");
    QFile file(bad);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not json");
    file.close();
    QVERIFY(DashboardGallery::infoForPath(bad).icon.isEmpty());
}

void TestDashboardGalleryIcon::setIconRoundTripsAndClears() {
    QTemporaryDir dir;
    const QJsonObject original{
        {"traceview", QJsonObject{{"formatVersion", 1}}},
        {"workspaces", QJsonObject{{"workspaces", QJsonArray{workspace("", 1)}}}}};
    const QString path = writeProject(dir, "demo.tvproj", original);

    QString error;
    QVERIFY2(DashboardGallery::setIconForPath(path, "lucide:star", &error), qPrintable(error));
    QCOMPARE(DashboardGallery::infoForPath(path).icon, QStringLiteral("lucide:star"));
    QVERIFY(DashboardGallery::infoForPath(path).iconIsCustom);
    QCOMPARE(DashboardGallery::infoForPath(path).widgets, 1);

    QVERIFY(DashboardGallery::setIconForPath(path, QString(), &error));
    QVERIFY(!DashboardGallery::infoForPath(path).iconIsCustom);
    // Back to exactly what it was: no leftover "gallery" object.
    QCOMPARE(readProject(path), original);
}

void TestDashboardGalleryIcon::setIconRefusesBuiltInExample() {
    QString error;
    QVERIFY(!DashboardGallery::setIconForPath(DashboardGallery::kBuiltInExamplePath,
                                              "lucide:star", &error));
    QVERIFY(!error.isEmpty());
}

}  // namespace

QTEST_MAIN(TestDashboardGalleryIcon)
#include "test_dashboardgallery_icon.moc"
