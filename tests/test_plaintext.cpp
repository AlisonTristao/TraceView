#include <QtTest>

#include "backend/plaintext.h"

using traceview::plainText;

// What a robot sends, as TraceView then shows it as plain text
// (backend/plaintext.h): a TinyShell result, an ESP-IDF log line, a message
// the robot cut in the middle of a colour sequence.
class TestPlainText : public QObject {
    Q_OBJECT

private slots:
    void shellColoursAreRemoved() {
        QCOMPARE(plainText(QStringLiteral("\x1b[33mbtp\x1b[39m \x1b[34m-status\x1b[0m: \x1b[32msuccess\x1b[0m")),
                 QStringLiteral("btp -status: success"));
    }
    void espIdfLogLineIsPlain() {
        QCOMPARE(plainText(QStringLiteral("\x1b[0;33mW (1234) wifi: beacon timeout\x1b[0m")),
                 QStringLiteral("W (1234) wifi: beacon timeout"));
    }
    void aSequenceCutShortGoesToo() {
        QCOMPARE(plainText(QStringLiteral("led -rgb: success\x1b[3")), QStringLiteral("led -rgb: success"));
        QCOMPARE(plainText(QStringLiteral("done\x1b")), QStringLiteral("done"));
    }
    void carriageReturnsGoNewlinesStay() {
        QCOMPARE(plainText(QStringLiteral("a\r\nb\r\n")), QStringLiteral("a\nb\n"));
    }
    void plainTextIsUntouched() {
        const QString json = QStringLiteral("{\"max\":4,\"networks\":[{\"ssid\":\"Pantanal 3 andar\"}]}");
        QCOMPARE(plainText(json), json);
        QCOMPARE(plainText(QStringLiteral("estação — ok ✓")), QStringLiteral("estação — ok ✓"));
    }
};

QTEST_APPLESS_MAIN(TestPlainText)
#include "test_plaintext.moc"
