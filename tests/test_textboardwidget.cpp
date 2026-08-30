#include <QFontDatabase>
#include <QFontMetricsF>
#include <QtTest>

#include "dashboard/widgets/textboardwidget.h"

using traceview::TextBoardWidget;

namespace {

// The exact `sys -health` report the robot now publishes on system.monitor.
const QString kHealthReport = QStringLiteral(
    "================= Monitor Stats =================\n"
    "uptime      : 00h 15m 00s\n"
    "---------------------- CPU ----------------------\n"
    "tick rate   :    1000 Hz\n"
    "core rate   :     240 MHz\n"
    "core temp   :   41.10 °C\n"
    "core PRO    :   91.81 %\n"
    "core APP    :   23.41 %\n"
    "-------------------- memory --------------------\n"
    "logger      :    1.71 %\n"
    "total ram   :  942.05 kb\n"
    "sram        :   91.72 kb\n"
    "psram       :  881.61 kb\n"
    "--------------------- tasks ---------------------\n"
    "task name        prio core      cpu     free heap\n"
    "-\n"
    "ipc1             24   app      0.00%     0.55 kb\n"
    "wifi             23   pro      1.42%     2.68 kb\n"
    "esp_timer        22   pro     11.29%     2.95 kb\n"
    "sys_evt          20   pro      0.00%     1.62 kb\n"
    "tcpip            18   any      0.03%     2.39 kb\n"
    "state_machine    10   app     23.41%     6.57 kb\n"
    "EKF_task         4    pro     39.27%     0.85 kb\n"
    "comms            4    pro     10.90%     4.54 kb\n"
    "routine          3    pro     26.00%     4.40 kb\n"
    "shell_task       2    pro      2.72%     4.27 kb\n"
    "system_monitor   1    pro      0.19%     1.70 kb\n"
    "Tmr Svc          1    any      0.00%     1.35 kb\n"
    "junkebox_task    1    pro      0.00%     3.13 kb\n"
    "mdns             1    pro      0.00%     3.28 kb\n"
    "ipc0             1    pro      0.00%     0.46 kb\n"
    "IDLE1            0    app     76.59%     0.75 kb\n"
    "IDLE0            0    pro      8.19%     0.74 kb\n"
    "=================================================\n");

// Matches TextBoardWidget's private boardFont() so the fit assertions below
// measure with the same metrics the widget used.
QFont probeFont(int pixelSize) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    font.setPixelSize(pixelSize);
    return font;
}

class TestTextBoardWidget : public QObject {
    Q_OBJECT

private slots:
    void textOperationsReplaceAppendAndClear();
    void onlyConfiguredTopicUpdatesTheBoard();
    void fontShrinksWithTheAvailableArea();
    void wholeHealthReportFitsWithoutClipping();
    void trailingNewlineDoesNotReserveABlankLine();
    void tinyCellStillReturnsAUsableSize();
    void blockIsCentredInTheCell();
};

void TestTextBoardWidget::textOperationsReplaceAppendAndClear() {
    TextBoardWidget board;
    board.setText(QStringLiteral("CPU: 10%"));
    board.appendText(QStringLiteral("\nRAM: 20%"));
    QCOMPARE(board.text(), QStringLiteral("CPU: 10%\nRAM: 20%"));
    board.clearText();
    QVERIFY(board.text().isEmpty());
}

void TestTextBoardWidget::onlyConfiguredTopicUpdatesTheBoard() {
    TextBoardWidget board;
    QJsonObject config;
    config["sourceId"] = QStringLiteral("0x11223344");
    config["topicId"] = QStringLiteral("0x0003");
    config["text"] = QStringLiteral("waiting");
    board.setConfig(config);
    QCOMPARE(board.text(), QStringLiteral("waiting"));

    board.onTextSample(0x11223344, 4, 1, QStringLiteral("wrong topic"));
    QCOMPARE(board.text(), QStringLiteral("waiting"));
    board.onTextSample(0x11223344, 3, 2, QStringLiteral("task  cpu\nEKF   12.3%"));
    QCOMPARE(board.text(), QStringLiteral("task  cpu\nEKF   12.3%"));
}

void TestTextBoardWidget::fontShrinksWithTheAvailableArea() {
    TextBoardWidget board;
    board.setText(QStringLiteral("task name        core    cpu    free stack\n"
                                 "state_machine   app   12.34%      4.25 kb\n"
                                 "system_monitor  pro    0.33%      3.75 kb"));
    board.resize(800, 300);
    const int large = board.fittedFontPixelSize();
    board.resize(260, 90);
    const int small = board.fittedFontPixelSize();

    QVERIFY(large > small);
    QVERIFY(small >= 3);
}

void TestTextBoardWidget::wholeHealthReportFitsWithoutClipping() {
    TextBoardWidget board;
    board.setText(kHealthReport);
    board.resize(620, 680);

    const int px = board.fittedFontPixelSize();
    QVERIFY(px >= 3);

    // At the fitted size every line of the report must fit inside the padded
    // content box -- that is the widget's whole contract ("always show the
    // complete document").
    const QStringList lines = QString(kHealthReport).chopped(1).split('\n');
    const QFontMetricsF metrics(probeFont(px));
    const qreal padding = 12.0;
    QVERIFY(metrics.lineSpacing() * lines.size() <= board.height() - 2 * padding);
    for (const QString& line : lines) {
        QVERIFY(metrics.horizontalAdvance(line) <= board.width() - 2 * padding);
    }
}

void TestTextBoardWidget::trailingNewlineDoesNotReserveABlankLine() {
    TextBoardWidget withNewline;
    withNewline.setText(QStringLiteral("alpha\nbeta\ngamma\n"));
    withNewline.resize(300, 220);

    TextBoardWidget withoutNewline;
    withoutNewline.setText(QStringLiteral("alpha\nbeta\ngamma"));
    withoutNewline.resize(300, 220);

    // A producer's terminating '\n' must not cost a line of vertical budget.
    QCOMPARE(withNewline.fittedFontPixelSize(), withoutNewline.fittedFontPixelSize());
}

void TestTextBoardWidget::tinyCellStillReturnsAUsableSize() {
    TextBoardWidget board;
    board.setText(kHealthReport);
    board.resize(200, 120);
    QVERIFY(board.fittedFontPixelSize() >= 3);
}

void TestTextBoardWidget::blockIsCentredInTheCell() {
    TextBoardWidget board;
    board.setText(kHealthReport);
    board.resize(900, 760);

    const QRectF block = board.textBlockRect();
    QVERIFY2(block.left() > 12.0, "block should not be pinned to the padding");
    QVERIFY2(block.top() > 12.0, "block should not be pinned to the padding");
    // Equal margins on each axis, modulo rounding.
    QVERIFY(qAbs(block.left() - (board.width() - block.width()) / 2.0) < 1.0);
    QVERIFY(qAbs(block.top() - (board.height() - block.height()) / 2.0) < 1.0);
}

}  // namespace

QTEST_MAIN(TestTextBoardWidget)
#include "test_textboardwidget.moc"
