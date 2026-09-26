#include <QtTest>

#include "core/appearancecatalog.h"
#include "traceview/appearance.h"
#include "traceview/thememanager.h"

using namespace traceview;

class TestAppearance : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void idsRoundTripWithDefaults();
    void customDataColorsKeepTheSeriesColor();
    void schemesCycleByIndex();
    void densityScalesTheChrome();
    void paletteJsonRoundTrips();
    void contrastRatioMatchesWcag();
    void everyPresetNamesRealChoices();
    void snapshotsRoundTripThroughJson();
};

void TestAppearance::initTestCase() {
    // Keep the developer's own QSettings out of this run.
    QCoreApplication::setOrganizationName(QStringLiteral("TraceViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("test_appearance"));
}

void TestAppearance::idsRoundTripWithDefaults() {
    for (DataColorsId id : allDataColors()) {
        QCOMPARE(dataColorsFromId(dataColorsIdString(id)), id);
    }
    for (DensityId id : allDensities()) {
        QCOMPARE(densityFromId(densityIdString(id)), id);
    }
    for (CardHeaderId id : allCardHeaders()) {
        QCOMPARE(cardHeaderFromId(cardHeaderIdString(id)), id);
    }
    for (CanvasId id : allCanvases()) {
        QCOMPARE(canvasFromId(canvasIdString(id)), id);
    }
    for (MotionId id : allMotions()) {
        QCOMPARE(motionFromId(motionIdString(id)), id);
    }
    // Unknown or missing ids fall back to the look TraceView always had.
    QCOMPARE(dataColorsFromId(QString()), DataColorsId::Custom);
    QCOMPARE(densityFromId("bogus"), DensityId::Normal);
    QCOMPARE(cardHeaderFromId(QString()), CardHeaderId::Filled);
    QCOMPARE(canvasFromId(QString()), CanvasId::Plain);
    QCOMPARE(motionFromId(QString()), MotionId::Animated);
}

void TestAppearance::customDataColorsKeepTheSeriesColor() {
    ThemePalette palette;
    palette.accent = QColor("#3366FF");
    const QColor own("#123456");
    QCOMPARE(dataSeriesColor(DataColorsId::Custom, 3, own, palette), own);
}

void TestAppearance::schemesCycleByIndex() {
    ThemePalette palette;
    palette.series = {QColor("#111111"), QColor("#222222")};
    QCOMPARE(dataSeriesColor(DataColorsId::Palette, 0, Qt::red, palette), QColor("#111111"));
    QCOMPARE(dataSeriesColor(DataColorsId::Palette, 3, Qt::red, palette), QColor("#222222"));
    QCOMPARE(dataSeriesColor(DataColorsId::Matlab, 0, Qt::red, palette), QColor("#0072BD"));
    QCOMPARE(dataSeriesColor(DataColorsId::Matlab, 7, Qt::red, palette), QColor("#0072BD"));
    QCOMPARE(dataSeriesColor(DataColorsId::OkabeIto, 1, Qt::red, palette), QColor("#E69F00"));
    // Neighboring monochrome series must not be the same shade.
    palette.accent = QColor("#3366FF");
    QVERIFY(dataSeriesColor(DataColorsId::Monochrome, 0, Qt::red, palette) !=
            dataSeriesColor(DataColorsId::Monochrome, 1, Qt::red, palette));
}

void TestAppearance::densityScalesTheChrome() {
    const Density& compact = density(DensityId::Compact);
    const Density& normal = density(DensityId::Normal);
    const Density& comfortable = density(DensityId::Comfortable);
    QCOMPARE(normal.headerHeight, 24);  // the original header
    QCOMPARE(normal.contentPadding, 12);
    QVERIFY(compact.headerHeight < normal.headerHeight);
    QVERIFY(comfortable.headerHeight > normal.headerHeight);
    QVERIFY(compact.gutterScale < 1.0 && comfortable.gutterScale > 1.0);
}

void TestAppearance::paletteJsonRoundTrips() {
    ThemePalette palette;
    palette.id = QStringLiteral("custom:abc");
    palette.displayName = QStringLiteral("Mine");
    palette.background = QColor("#010203");
    palette.border = QColor(255, 255, 255, 40);  // alpha survives
    palette.series = {QColor("#AABBCC"), QColor("#DDEEFF")};
    ThemePalette fallback;
    fallback.accent = QColor("#FF0000");
    const ThemePalette back = paletteFromJson(paletteToJson(palette), fallback);
    QCOMPARE(back.id, palette.id);
    QCOMPARE(back.displayName, palette.displayName);
    QCOMPARE(back.background, palette.background);
    QCOMPARE(back.border, palette.border);
    QCOMPARE(back.series, palette.series);
    QVERIFY(isCustomPaletteId(back.id));
    QVERIFY(!isCustomPaletteId(QStringLiteral("dark")));
}

void TestAppearance::contrastRatioMatchesWcag() {
    QCOMPARE(qRound(contrastRatio(Qt::black, Qt::white)), 21);
    QCOMPARE(contrastRatio(Qt::white, Qt::white), 1.0);
    // Order doesn't matter.
    QCOMPARE(contrastRatio(QColor("#777777"), Qt::white),
             contrastRatio(Qt::white, QColor("#777777")));
}

void TestAppearance::everyPresetNamesRealChoices() {
    const QVector<AppearanceOption> options = appearanceOptions();
    for (const AppearancePreset& preset : appearancePresets()) {
        if (!preset.builtIn) {
            continue;
        }
        for (const AppearanceOption& option : options) {
            if (!option.inSnapshot) {
                QVERIFY2(!preset.values.contains(option.key), qPrintable(preset.id));
                continue;
            }
            QVERIFY2(preset.values.contains(option.key),
                     qPrintable(preset.id + " lacks " + option.key));
            bool found = false;
            for (const AppearanceChoice& choice : option.choices()) {
                found = found || choice.id == preset.values.value(option.key);
            }
            QVERIFY2(found, qPrintable(preset.id + ": unknown " + option.key + " " +
                                       preset.values.value(option.key)));
        }
    }
}

void TestAppearance::snapshotsRoundTripThroughJson() {
    const AppearanceSnapshot snapshot = currentAppearance();
    QVERIFY(!snapshot.isEmpty());
    QVERIFY(!snapshot.contains(QStringLiteral("motion")));  // not part of a look
    QCOMPARE(appearanceFromJson(appearanceToJson(snapshot)), snapshot);
}

QTEST_MAIN(TestAppearance)
#include "test_appearance.moc"
