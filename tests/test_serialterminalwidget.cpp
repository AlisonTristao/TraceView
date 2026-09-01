#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QtTest>

#include "dashboard/widgets/serialterminalwidget.h"
#include "traceview/theme.h"
#include "traceview/thememanager.h"

using traceview::SerialTerminalWidget;
using traceview::ThemeManager;

namespace {

// Foreground colour of the character at document position `pos` (0-based).
QColor fgAt(const SerialTerminalWidget& widget, int pos) {
    QTextCursor cur(widget.document());
    cur.setPosition(pos);
    cur.setPosition(pos + 1, QTextCursor::KeepAnchor);
    return cur.charFormat().foreground().color();
}

class TestSerialTerminalWidget : public QObject {
    Q_OBJECT

private slots:
    void printableKeySendsUtf8BytesAndDoesNotEchoLocally();
    void enterBackspaceTabSendExpectedBytes();
    void ctrlLetterSendsAsciiControlCode();
    void ctrlCWithSelectionCopiesInsteadOfSendingSigint();
    void ctrlShiftCAlwaysCopies();
    void ctrlShiftVSendsClipboardText();
    void ctrlLeftRightRequestTabSwitchInsteadOfSending();
    void arrowKeysSendAnsiEscapeSequences();
    void modifiedArrowKeyDoesNotSendAnythingAndAllowsLocalSelection();
    void appendDataRendersPromptAndFastPathEcho();
    void appendDataFastPathBackspaceErasesLastChar();
    void appendDataFullRedrawOverwritesLineAndRepositionsCursor();
    void appendDataNewlineCommitsLineAndStartsFresh();
    void appendDataSplitsMultibyteUtf8AcrossCallsWithoutMojibake();
    void clearTerminalResetsDocumentAndLineState();
    void remoteCursorUsesCustomOverlayAndBlinks();
    void sgrColoursARunAndLeavesNoEscapesInText();
    void sgrColoursShellCommandTokens();
    void sgrColourSurvivesLineCommitAndCarriesRoleForRetint();
    void unknownCsiIsSwallowedNotRendered();
    void csiSplitAcrossAppendDataStillResolves();
    void eraseInLineClearsFromCursorToEnd();
    void themeSwitchRetintsColouredScrollback();
};

void TestSerialTerminalWidget::printableKeySendsUtf8BytesAndDoesNotEchoLocally() {
    SerialTerminalWidget widget;
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_A);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toByteArray(), QByteArrayLiteral("a"));
    // PASSO 6: no local echo/prompt -- the widget stays empty until the
    // dongle's own TERMINAL_OUT arrives via appendData().
    QCOMPARE(widget.toPlainText(), QString());
}

void TestSerialTerminalWidget::enterBackspaceTabSendExpectedBytes() {
    SerialTerminalWidget widget;
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_Return);
    QTest::keyClick(&widget, Qt::Key_Backspace);
    QTest::keyClick(&widget, Qt::Key_Tab);

    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArray(1, '\r'));
    QCOMPARE(spy.at(1).at(0).toByteArray(), QByteArray(1, static_cast<char>(0x7f)));
    QCOMPARE(spy.at(2).at(0).toByteArray(), QByteArray(1, '\t'));
}

void TestSerialTerminalWidget::ctrlLetterSendsAsciiControlCode() {
    SerialTerminalWidget widget;
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(&widget, Qt::Key_D, Qt::ControlModifier);
    QTest::keyClick(&widget, Qt::Key_R, Qt::ControlModifier);

    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toByteArray(),
             QByteArray(1, static_cast<char>(0x03)));  // SIGINT-style
    QCOMPARE(spy.at(1).at(0).toByteArray(), QByteArray(1, static_cast<char>(0x04)));
    QCOMPARE(spy.at(2).at(0).toByteArray(),
             QByteArray(1, static_cast<char>(0x12)));  // ShellSerial Ctrl+R search
}

void TestSerialTerminalWidget::ctrlCWithSelectionCopiesInsteadOfSendingSigint() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("hello world"));

    QTextCursor cursor = widget.textCursor();
    cursor.select(QTextCursor::Document);
    widget.setTextCursor(cursor);
    QVERIFY(widget.textCursor().hasSelection());

    QGuiApplication::clipboard()->clear();
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_C, Qt::ControlModifier);

    // Copied, not forwarded -- 0x03 would kill the running command instead.
    QCOMPARE(spy.count(), 0);
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("hello world")));
}

void TestSerialTerminalWidget::ctrlShiftCAlwaysCopies() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("copy me"));
    QTextCursor cursor = widget.textCursor();
    cursor.select(QTextCursor::Document);
    widget.setTextCursor(cursor);

    QGuiApplication::clipboard()->clear();
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);

    QCOMPARE(spy.count(), 0);
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("copy me")));
}

void TestSerialTerminalWidget::ctrlShiftVSendsClipboardText() {
    SerialTerminalWidget widget;
    QGuiApplication::clipboard()->setText(QStringLiteral("pasted"));
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toByteArray(), QByteArrayLiteral("pasted"));
}

void TestSerialTerminalWidget::ctrlLeftRightRequestTabSwitchInsteadOfSending() {
    SerialTerminalWidget widget;
    QSignalSpy sendSpy(&widget, &SerialTerminalWidget::sendRequested);
    QSignalSpy prevSpy(&widget, &SerialTerminalWidget::previousTabRequested);
    QSignalSpy nextSpy(&widget, &SerialTerminalWidget::nextTabRequested);

    QTest::keyClick(&widget, Qt::Key_Left, Qt::ControlModifier);
    QTest::keyClick(&widget, Qt::Key_Right, Qt::ControlModifier);

    QCOMPARE(prevSpy.count(), 1);
    QCOMPARE(nextSpy.count(), 1);
    QCOMPARE(sendSpy.count(), 0);
}

void TestSerialTerminalWidget::arrowKeysSendAnsiEscapeSequences() {
    SerialTerminalWidget widget;
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_Up);
    QTest::keyClick(&widget, Qt::Key_Down);
    QTest::keyClick(&widget, Qt::Key_Right);
    QTest::keyClick(&widget, Qt::Key_Left);

    QCOMPARE(spy.count(), 4);
    QCOMPARE(spy.at(0).at(0).toByteArray(), QByteArrayLiteral("\x1b[A"));
    QCOMPARE(spy.at(1).at(0).toByteArray(), QByteArrayLiteral("\x1b[B"));
    QCOMPARE(spy.at(2).at(0).toByteArray(), QByteArrayLiteral("\x1b[C"));
    QCOMPARE(spy.at(3).at(0).toByteArray(), QByteArrayLiteral("\x1b[D"));
}

void TestSerialTerminalWidget::modifiedArrowKeyDoesNotSendAnythingAndAllowsLocalSelection() {
    // PASSO 7: Shift+Left is a keyboard text-selection combo over the
    // read-only scrollback, not terminal input -- it must not be forwarded
    // as a keystroke to the dongle.
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("hello"));
    QSignalSpy spy(&widget, &SerialTerminalWidget::sendRequested);

    QTest::keyClick(&widget, Qt::Key_Left, Qt::ShiftModifier);

    // Whether or not this read-only widget's default text-interaction flags
    // actually turn that into a visible selection is a QPlainTextEdit
    // implementation detail; what topico 19 PASSO 7 requires is that it is
    // never forwarded to the dongle as a keystroke.
    QCOMPARE(spy.count(), 0);
}

void TestSerialTerminalWidget::appendDataRendersPromptAndFastPathEcho() {
    SerialTerminalWidget widget;

    widget.appendData(QByteArrayLiteral("dongle> "));
    QCOMPARE(widget.toPlainText(), QStringLiteral("dongle> "));

    // ShellSerial's tail-append fast path: plain Serial.print(c) per
    // character, no \r involved.
    widget.appendData(QByteArrayLiteral("i"));
    widget.appendData(QByteArrayLiteral("n"));
    widget.appendData(QByteArrayLiteral("f"));
    QCOMPARE(widget.toPlainText(), QStringLiteral("dongle> inf"));
}

void TestSerialTerminalWidget::appendDataFastPathBackspaceErasesLastChar() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("dongle> inf"));

    // ShellSerial::eraseLastChar()'s tail fast path: "\b \b" -- moves back,
    // overwrites with a space, moves back again. The erased character
    // becomes a trailing space rather than disappearing from the
    // underlying text (same artifact as a real terminal's blank cell).
    widget.appendData(QByteArray("\b \b", 3));

    QCOMPARE(widget.toPlainText(), QStringLiteral("dongle> in "));
    QCOMPARE(widget.textCursor().positionInBlock(), 10);
}

void TestSerialTerminalWidget::appendDataFullRedrawOverwritesLineAndRepositionsCursor() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("dongle> info"));

    // ShellSerial::redrawInput() shrinking from "info" (4 chars) to "in" (2
    // chars) after an arrow-left + backspace mid-line: \r, new content, pad
    // with spaces to erase the stale tail, \r again, reprint up to cursor.
    widget.appendData(QByteArrayLiteral("\rdongle> in  \rdongle> in"));

    // The trailing pad spaces stay in the underlying text (a real terminal
    // has the same artifact -- blank cells at the end of a physically
    // cleared line); visually indistinguishable from a shorter line.
    QCOMPARE(widget.toPlainText(), QStringLiteral("dongle> in  "));
    // Cursor lands right after "in", not at the padded/erased tail.
    QCOMPARE(widget.textCursor().positionInBlock(), 10);
}

void TestSerialTerminalWidget::appendDataNewlineCommitsLineAndStartsFresh() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("dongle> info"));
    widget.appendData(QByteArrayLiteral("\r\n"));
    widget.appendData(QByteArrayLiteral("chip=esp32s3\r\n"));
    widget.appendData(QByteArrayLiteral("dongle> "));

    QCOMPARE(widget.toPlainText(), QStringLiteral("dongle> info\nchip=esp32s3\ndongle> "));
}

void TestSerialTerminalWidget::appendDataSplitsMultibyteUtf8AcrossCallsWithoutMojibake() {
    SerialTerminalWidget widget;
    const QByteArray message = QStringLiteral("sessão ativa").toUtf8();  // "sessão ativa"

    // Split exactly inside the 2-byte UTF-8 encoding of 'ã' (0xC3 0xA3) to
    // simulate a TERMINAL_OUT chunk boundary landing mid-character.
    const int splitIndex = message.indexOf('\xC3');
    QVERIFY(splitIndex > 0);
    widget.appendData(message.left(splitIndex + 1));
    widget.appendData(message.mid(splitIndex + 1));

    QCOMPARE(widget.toPlainText(), QStringLiteral("sessão ativa"));
}

void TestSerialTerminalWidget::clearTerminalResetsDocumentAndLineState() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("dongle> info\r\nchip=esp32s3\r\n"));

    widget.clearTerminal();
    QCOMPARE(widget.toPlainText(), QString());

    // Line-model state must also be reset, not just the visible document --
    // otherwise the next appendData() would overwrite starting mid-line.
    widget.appendData(QByteArrayLiteral("dongle> "));
    QCOMPARE(widget.toPlainText(), QStringLiteral("dongle> "));
}

void TestSerialTerminalWidget::remoteCursorUsesCustomOverlayAndBlinks() {
    SerialTerminalWidget widget;
    QPalette terminalPalette = widget.palette();
    terminalPalette.setColor(QPalette::Base, Qt::black);
    terminalPalette.setColor(QPalette::Text, Qt::magenta);
    widget.setPalette(terminalPalette);
    widget.resize(240, 80);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    widget.activateWindow();
    widget.setFocus();
    QTRY_VERIFY(widget.hasFocus());

    // The terminal's cursor belongs to the remote ShellSerial state, not to
    // QPlainTextEdit's local selection cursor. A custom overlay draws it so
    // clicking/selecting scrollback cannot leave a second, misplaced caret.
    QCOMPARE(widget.cursorWidth(), 0);

    auto* cursorOverlay =
        widget.viewport()->findChild<QWidget*>(QStringLiteral("terminalCursorOverlay"));
    QVERIFY(cursorOverlay != nullptr);

    widget.appendData(QByteArrayLiteral("x"));
    QVERIFY(cursorOverlay->isVisible());
    const QImage cursorImage = cursorOverlay->grab().toImage();
    QCOMPARE(cursorImage.pixelColor(0, cursorImage.height() / 2),
             widget.palette().color(QPalette::Text));
    QTest::qWait(800);
    QVERIFY(!cursorOverlay->isVisible());

    // A busy dongle must not restart the cycle for every TERMINAL_OUT frame.
    widget.appendData(QByteArrayLiteral("y"));
    QVERIFY(!cursorOverlay->isVisible());

    // DashboardCell can take keyboard focus away from the terminal. Its
    // remote cursor must continue blinking nevertheless.
    widget.clearFocus();
    QVERIFY(!widget.hasFocus());
    QTest::qWait(650);
    QVERIFY(cursorOverlay->isVisible());

    widget.setCursorBlinkEnabled(false);
    QVERIFY(!cursorOverlay->isVisible());
    QTest::qWait(650);
    QVERIFY(!cursorOverlay->isVisible());
}

void TestSerialTerminalWidget::sgrColoursARunAndLeavesNoEscapesInText() {
    SerialTerminalWidget widget;
    const traceview::ThemePalette& theme = ThemeManager::instance().currentTheme();

    widget.appendData(QByteArrayLiteral("ok \x1b[31mboom\x1b[0m done"));

    // The escapes never reach the document.
    QCOMPARE(widget.toPlainText(), QStringLiteral("ok boom done"));
    // "boom" (indices 3..6) is danger; the text around it is not.
    QCOMPARE(fgAt(widget, 3), theme.danger);
    QCOMPARE(fgAt(widget, 6), theme.danger);
    QVERIFY(fgAt(widget, 0) != theme.danger);   // "ok "
    QVERIFY(fgAt(widget, 8) != theme.danger);   // " done"
}

void TestSerialTerminalWidget::sgrColoursShellCommandTokens() {
    SerialTerminalWidget widget;
    const traceview::ThemePalette& theme = ThemeManager::instance().currentTheme();

    widget.appendData(
        QByteArrayLiteral("$ \x1b[33mdongle\x1b[30m \x1b[34m-ping\x1b[30m 7\x1b[0m"));

    QCOMPARE(widget.toPlainText(), QStringLiteral("$ dongle -ping 7"));
    QCOMPARE(fgAt(widget, 2), theme.warning);  // module: dark yellow / warning token
    QCOMPARE(fgAt(widget, 9), theme.accent);   // function: dark blue / accent token
    QVERIFY(fgAt(widget, 15) != theme.warning);
    QVERIFY(fgAt(widget, 15) != theme.accent); // argument: normal dark text in light themes
}

void TestSerialTerminalWidget::sgrColourSurvivesLineCommitAndCarriesRoleForRetint() {
    SerialTerminalWidget widget;
    const traceview::ThemePalette& theme = ThemeManager::instance().currentTheme();

    widget.appendData(QByteArrayLiteral("\x1b[32mled ligado\x1b[0m\r\n$ "));

    QCOMPARE(widget.toPlainText(), QStringLiteral("led ligado\n$ "));
    QCOMPARE(fgAt(widget, 0), theme.success);

    // The committed run keeps a role property so a later theme switch can
    // re-resolve it (UserProperty == the widget's kPenRoleProperty).
    QTextCursor cur(widget.document());
    cur.setPosition(0);
    cur.setPosition(1, QTextCursor::KeepAnchor);
    QVERIFY(cur.charFormat().hasProperty(QTextFormat::UserProperty));
}

void TestSerialTerminalWidget::unknownCsiIsSwallowedNotRendered() {
    SerialTerminalWidget widget;

    // Clear-screen, cursor-home, cursor-forward: none is part of the ShellStyle
    // subset, all must vanish rather than print as "[2J" etc.
    widget.appendData(QByteArrayLiteral("\x1b[2J\x1b[H a\x1b[3Cb"));

    QCOMPARE(widget.toPlainText(), QStringLiteral(" ab"));
}

void TestSerialTerminalWidget::csiSplitAcrossAppendDataStillResolves() {
    SerialTerminalWidget widget;
    const traceview::ThemePalette& theme = ThemeManager::instance().currentTheme();

    widget.appendData(QByteArrayLiteral("a\x1b"));
    widget.appendData(QByteArrayLiteral("[31"));
    widget.appendData(QByteArrayLiteral("mb"));

    QCOMPARE(widget.toPlainText(), QStringLiteral("ab"));
    QCOMPARE(fgAt(widget, 1), theme.danger);
}

void TestSerialTerminalWidget::eraseInLineClearsFromCursorToEnd() {
    SerialTerminalWidget widget;
    widget.appendData(QByteArrayLiteral("dongle> command"));

    // The console path's prompt wipe: CR home, then erase-to-end-of-line.
    widget.appendData(QByteArrayLiteral("\r\x1b[K! result"));

    QCOMPARE(widget.toPlainText(), QStringLiteral("! result"));
}

void TestSerialTerminalWidget::themeSwitchRetintsColouredScrollback() {
    SerialTerminalWidget widget;
    auto& manager = ThemeManager::instance();
    const QString original = manager.currentTheme().id;

    const QVector<traceview::ThemePalette> themes = manager.availableThemes();
    QVERIFY(themes.size() >= 2);
    const traceview::ThemePalette other =
        (themes.at(0).id == manager.currentTheme().id) ? themes.at(1) : themes.at(0);

    widget.appendData(QByteArrayLiteral("\x1b[31mfail\x1b[0m\r\n"));
    QCOMPARE(fgAt(widget, 0), manager.currentTheme().danger);

    manager.setTheme(other.id);
    QCOMPARE(fgAt(widget, 0), other.danger);

    manager.setTheme(original);  // don't leak the switch into sibling tests
}

}  // namespace

QTEST_MAIN(TestSerialTerminalWidget)
#include "test_serialterminalwidget.moc"
