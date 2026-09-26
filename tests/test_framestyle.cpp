#include <QtTest>

#include "traceview/framestyle.h"

using traceview::allFrameStyles;
using traceview::FrameCorner;
using traceview::frameShapePath;
using traceview::frameStyle;
using traceview::FrameStyleId;
using traceview::frameStyleFromId;
using traceview::frameStyleIdString;
using traceview::scaleStyleSheetRadii;

class TestFrameStyle : public QObject {
    Q_OBJECT

private slots:
    void idsRoundTrip();
    void roundedIsTheOriginalLook();
    void squareAndChamferedShapes();
    void chamferKeepsRequestedCornersSquare();
    void borderlessDropsTheIdleOutline();
    void stylesheetRadiiScale();
};

void TestFrameStyle::idsRoundTrip() {
    for (FrameStyleId id : allFrameStyles()) {
        QCOMPARE(frameStyleFromId(frameStyleIdString(id)), id);
        QCOMPARE(frameStyle(id).id, id);
    }
    QCOMPARE(frameStyleFromId("bogus"), FrameStyleId::Rounded);
    QCOMPARE(frameStyleFromId(QString()), FrameStyleId::Rounded);
}

void TestFrameStyle::roundedIsTheOriginalLook() {
    const auto& rounded = frameStyle(FrameStyleId::Rounded);
    QCOMPARE(rounded.corner, FrameCorner::Round);
    QCOMPARE(rounded.cornerSize, 6.0);
    QCOMPARE(rounded.borderWidth, 2.0);
    QVERIFY(rounded.idleOutline);
    QCOMPARE(rounded.controlRadiusScale, 1.0);
}

void TestFrameStyle::squareAndChamferedShapes() {
    const QRectF r(0, 0, 100, 60);
    const QPainterPath square = frameShapePath(r, frameStyle(FrameStyleId::Square));
    QVERIFY(square.contains(QPointF(0.5, 0.5)));  // the corner itself is inside

    const QPainterPath chamfered = frameShapePath(r, frameStyle(FrameStyleId::Chamfered));
    QVERIFY(!chamfered.contains(QPointF(1, 1)));      // cut away
    QVERIFY(chamfered.contains(QPointF(10, 10)));     // past the 8px cut
    QVERIFY(!chamfered.contains(QPointF(99, 59)));
    QCOMPARE(chamfered.boundingRect(), r);
}

void TestFrameStyle::chamferKeepsRequestedCornersSquare() {
    // A widget under its cell header keeps square top corners.
    const QRectF r(0, 0, 100, 60);
    const QPainterPath path = frameShapePath(r, frameStyle(FrameStyleId::Chamfered), false,
                                             false, true, true);
    QVERIFY(path.contains(QPointF(1, 1)));
    QVERIFY(path.contains(QPointF(98.5, 1)));
    QVERIFY(!path.contains(QPointF(1, 59)));
}

void TestFrameStyle::borderlessDropsTheIdleOutline() {
    const auto& borderless = frameStyle(FrameStyleId::Borderless);
    QVERIFY(!borderless.idleOutline);
    QCOMPARE(borderless.borderWidth, 0.0);
    QCOMPARE(borderless.corner, FrameCorner::Round);
}

void TestFrameStyle::stylesheetRadiiScale() {
    const QString css = QStringLiteral(
        "QPushButton { border-radius: 4px; }\n"
        "QTabBar::tab { border-top-right-radius: 3px; border-bottom-left-radius: 8px; }\n"
        "QFrame { border: 1px solid red; padding: 4px; }");
    QCOMPARE(scaleStyleSheetRadii(css, 1.0), css);

    const QString square = scaleStyleSheetRadii(css, 0.0);
    QVERIFY(square.contains("border-radius: 0px"));
    QVERIFY(square.contains("border-top-right-radius: 0px"));
    QVERIFY(square.contains("border-bottom-left-radius: 0px"));
    // Only radii change.
    QVERIFY(square.contains("border: 1px solid red; padding: 4px;"));

    QVERIFY(scaleStyleSheetRadii(css, 2.0).contains("border-radius: 8px"));
}

QTEST_MAIN(TestFrameStyle)
#include "test_framestyle.moc"
