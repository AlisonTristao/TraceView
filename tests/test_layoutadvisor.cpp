#include <QJsonObject>
#include <QtTest>

#include "dashboard/layoutadvisor.h"

using namespace traceview;

// The layout advice of dashboard/layoutadvisor.h: per-kind hints and the
// review check_layout (MCP) and tools/chart_preview run.
class TestLayoutAdvisor : public QObject {
    Q_OBJECT

private slots:
    void everyKindHasSaneHints();
    void gaugeShapeChangesTheHints();
    void hintsJsonCarriesCells();
    void giantButtonIsTooLarge();
    void tinyChartIsTooSmall();
    void controlInFreeZoneIsFine();
    void controlOverDataIsAProblem();
    void chartsMayNotOverlap();
    void almostAlignedControlsAreFlagged();
    void stackedWidgetsShareEdges();
    void emptyAreasAreFlagged();
    void unusedFreeZonesAreListed();
    void offCanvasIsAProblem();
};

namespace {

LayoutReviewItem item(const QString& id, const QString& type, const QRectF& rect,
                      const QJsonObject& config = {}) {
    return {id, id, type, config, rect};
}

bool hasIssue(const LayoutReview& review, const QString& widgetId, const QString& kind) {
    for (const LayoutIssue& issue : review.issues) {
        if (issue.widgetId == widgetId && issue.kind == kind) {
            return true;
        }
    }
    return false;
}

const QSizeF kCanvas(1280, 720);

}  // namespace

void TestLayoutAdvisor::everyKindHasSaneHints() {
    for (const QString& type :
         {QString("push_button"), QString("toggle_switch"), QString("slider"), QString("led"),
          QString("dummy_line"), QString("dummy_bar"), QString("dummy_gauge"),
          QString("serial_monitor"), QString("text_board"), QString("scene"),
          QString("audio_spectrum"), QString("chat"), QString("robot_log"), QString("unknown")}) {
        const WidgetLayoutHints h = widgetLayoutHints(type, {});
        QVERIFY2(h.minSize.width() > 0 && h.minSize.height() > 0, qPrintable(type));
        QVERIFY2(h.idealSize.width() >= h.minSize.width(), qPrintable(type));
        QVERIFY2(h.idealSize.height() >= h.minSize.height(), qPrintable(type));
        if (h.maxSize.width() > 0) {
            QVERIFY2(h.maxSize.width() >= h.idealSize.width(), qPrintable(type));
            QVERIFY2(h.maxSize.height() >= h.idealSize.height(), qPrintable(type));
        }
        for (const QRectF& zone : h.freeZones) {
            QVERIFY2(QRectF(0, 0, 1, 1).contains(zone), qPrintable(type));
        }
        QVERIFY2(!h.freeZones.isEmpty() == h.acceptsOverlay, qPrintable(type));
        QVERIFY2(!h.note.isEmpty(), qPrintable(type));
    }
    QVERIFY(widgetLayoutHints("led", {}).canOverlay);
    QVERIFY(!widgetLayoutHints("dummy_bar", {}).canOverlay);
}

void TestLayoutAdvisor::gaugeShapeChangesTheHints() {
    const auto shaped = [](const char* shape) {
        return widgetLayoutHints("dummy_gauge",
                                 {{"view", QJsonObject{{"gaugeShape", QString(shape)}}}});
    };
    QVERIFY(shaped("arc").acceptsOverlay);
    QVERIFY(!shaped("meter").acceptsOverlay);
    QVERIFY(shaped("segments").minAspect >= shaped("arc").maxAspect);
}

void TestLayoutAdvisor::hintsJsonCarriesCells() {
    const QJsonObject json =
        widgetLayoutHintsJson(widgetLayoutHints("push_button", {}), QSizeF(20, 18));
    QCOMPARE(json["ideal"].toObject()["widthPx"].toInt(), 132);
    QCOMPARE(json["ideal"].toObject()["cols"].toInt(), 7);
    QCOMPARE(json["ideal"].toObject()["rows"].toInt(), 3);
    QVERIFY(json["canOverlay"].toBool());
    QVERIFY(json.contains("max"));
}

void TestLayoutAdvisor::giantButtonIsTooLarge() {
    const LayoutReview review =
        reviewLayout({item("start", "push_button", QRectF(0, 0, 160, 180))}, kCanvas);
    QVERIFY(hasIssue(review, "start", "tooLarge"));
    const LayoutIssue& issue = review.issues.first();
    QVERIFY(issue.suggestedRect.height() <= 80);
    QCOMPARE(issue.suggestedRect.center(), QPointF(80, 90));
}

void TestLayoutAdvisor::tinyChartIsTooSmall() {
    const LayoutReview review =
        reviewLayout({item("chart", "dummy_line", QRectF(0, 0, 200, 100))}, kCanvas);
    QVERIFY(hasIssue(review, "chart", "tooSmall"));
}

void TestLayoutAdvisor::controlInFreeZoneIsFine() {
    // The line chart's free zone is its upper left (x 10-38%, y 18-42%).
    const QRectF chart(0, 0, 640, 360);
    const QRectF button(80, 75, 120, 50);
    const LayoutReview review = reviewLayout(
        {item("chart", "dummy_line", chart), item("tare", "push_button", button)}, kCanvas);
    QVERIFY(!hasIssue(review, "tare", "covers"));
}

void TestLayoutAdvisor::controlOverDataIsAProblem() {
    // Over the newest samples, on the right.
    const QRectF chart(0, 0, 640, 360);
    const LayoutReview review =
        reviewLayout({item("chart", "dummy_line", chart),
                      item("tare", "push_button", QRectF(500, 150, 120, 50))},
                     kCanvas);
    QVERIFY(hasIssue(review, "tare", "covers"));
    // ...with a suggestion inside the free zone.
    for (const LayoutIssue& issue : review.issues) {
        if (issue.kind == QLatin1String("covers")) {
            QVERIFY(!issue.suggestedRect.isNull());
            QVERIFY(QRectF(60, 60, 190, 95).contains(issue.suggestedRect));
        }
    }
}

void TestLayoutAdvisor::chartsMayNotOverlap() {
    const LayoutReview review = reviewLayout({item("a", "dummy_line", QRectF(0, 0, 640, 360)),
                                              item("b", "dummy_bar", QRectF(600, 0, 360, 360))},
                                             kCanvas);
    QVERIFY(hasIssue(review, "b", "covers"));
}

void TestLayoutAdvisor::almostAlignedControlsAreFlagged() {
    const LayoutReview review = reviewLayout({item("a", "push_button", QRectF(0, 600, 130, 54)),
                                              item("b", "push_button", QRectF(140, 606, 130, 54))},
                                             kCanvas);
    QVERIFY(hasIssue(review, "b", "alignment"));

    const LayoutReview aligned = reviewLayout({item("a", "push_button", QRectF(0, 600, 130, 54)),
                                               item("b", "push_button", QRectF(140, 600, 130, 54))},
                                              kCanvas);
    QVERIFY(!hasIssue(aligned, "b", "alignment"));
}

void TestLayoutAdvisor::stackedWidgetsShareEdges() {
    // A chart over another whose right edge is 10 px short.
    const LayoutReview review = reviewLayout({item("a", "dummy_line", QRectF(0, 0, 640, 360)),
                                              item("b", "dummy_line", QRectF(0, 360, 630, 360))},
                                             kCanvas);
    QVERIFY(hasIssue(review, "b", "alignment"));
    for (const LayoutIssue& issue : review.issues) {
        if (issue.kind == QLatin1String("alignment")) {
            QCOMPARE(issue.suggestedRect.right(), 640.0);
        }
    }
}

void TestLayoutAdvisor::emptyAreasAreFlagged() {
    // Two charts filling the canvas but for a 2-column gap between them.
    const double cell = 1280.0 / 60.0;
    const LayoutReview gap =
        reviewLayout({item("a", "dummy_line", QRectF(0, 0, 29 * cell, 720)),
                      item("b", "dummy_line", QRectF(31 * cell, 0, 29 * cell, 720))},
                     kCanvas);
    QVERIFY(hasIssue(gap, QString(), "emptyArea"));

    const LayoutReview tiled =
        reviewLayout({item("a", "dummy_line", QRectF(0, 0, 30 * cell, 720)),
                      item("b", "dummy_line", QRectF(30 * cell, 0, 30 * cell, 720))},
                     kCanvas);
    QVERIFY(!hasIssue(tiled, QString(), "emptyArea"));
}

void TestLayoutAdvisor::unusedFreeZonesAreListed() {
    const QRectF ring(0, 0, 300, 300);
    const LayoutReview review = reviewLayout({item("ring", "dummy_gauge", ring)}, kCanvas);
    QCOMPARE(review.overlaySpots.size(), 2);  // both bottom corners

    const LayoutReview used = reviewLayout(
        {item("ring", "dummy_gauge", ring), item("led", "led", QRectF(242, 242, 56, 56))}, kCanvas);
    QCOMPARE(used.overlaySpots.size(), 1);
    QVERIFY(!hasIssue(used, "led", "covers"));
}

void TestLayoutAdvisor::offCanvasIsAProblem() {
    const LayoutReview review =
        reviewLayout({item("chart", "dummy_line", QRectF(1000, 0, 600, 320))}, kCanvas);
    QVERIFY(hasIssue(review, "chart", "offCanvas"));
}

QTEST_GUILESS_MAIN(TestLayoutAdvisor)
#include "test_layoutadvisor.moc"
