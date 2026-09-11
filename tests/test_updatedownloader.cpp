#include <QtTest/QtTest>

#include "updater/updatedownloader.h"

using namespace traceview;

// findChecksum() is the parsing half of UpdateDownloader's integrity check --
// pulled out into a free function so it's testable without a live download,
// same reasoning as test_updatechecker.cpp's own comment. Covers the exact
// regression release.yml shipped once: a Windows checksum line written with
// no trailing newline gets glued onto the next line by the publish job's
// `cat`, and the parser must still refuse rather than silently match.

namespace {

class TestUpdateDownloader : public QObject {
    Q_OBJECT

private slots:
    void findsMatchingLine();
    void isCaseInsensitiveOnFilename();
    void returnsEmptyWhenNotListed();
    void returnsEmptyWhenLinesAreRunTogether();
};

void TestUpdateDownloader::findsMatchingLine() {
    const QByteArray sums =
        "abc123  TraceView-2.5.3-windows-x64.exe\n"
        "def456  TraceView-2.5.3-linux-x64.tar.gz\n";
    QCOMPARE(findChecksum(sums, QStringLiteral("TraceView-2.5.3-windows-x64.exe")),
             QStringLiteral("abc123"));
    QCOMPARE(findChecksum(sums, QStringLiteral("TraceView-2.5.3-linux-x64.tar.gz")),
             QStringLiteral("def456"));
}

void TestUpdateDownloader::isCaseInsensitiveOnFilename() {
    const QByteArray sums = "ABC123  TraceView-2.5.3-windows-x64.exe\n";
    QCOMPARE(findChecksum(sums, QStringLiteral("traceview-2.5.3-windows-x64.exe")),
             QStringLiteral("abc123"));
}

void TestUpdateDownloader::returnsEmptyWhenNotListed() {
    const QByteArray sums = "abc123  TraceView-2.5.3-linux-x64.tar.gz\n";
    QVERIFY(findChecksum(sums, QStringLiteral("TraceView-2.5.3-windows-x64.exe")).isEmpty());
}

void TestUpdateDownloader::returnsEmptyWhenLinesAreRunTogether() {
    // Reproduces the exact release.yml bug: `Out-File -NoNewline` on the
    // Windows line meant `cat sha256-windows.txt sha256-linux.txt` glued the
    // two together with no separator -- the Windows filename ends up in the
    // MIDDLE of the resulting single line, not as its last token, so it must
    // not match, even though a human skimming the file might think it's there.
    const QByteArray sums =
        "abc123  TraceView-2.5.3-windows-x64.exedef456  TraceView-2.5.3-linux-x64.tar.gz\n";
    QVERIFY(findChecksum(sums, QStringLiteral("TraceView-2.5.3-windows-x64.exe")).isEmpty());
    // The merged line's last token IS the Linux filename, so that lookup
    // still "succeeds" -- but with the Windows line's hash (its own first
    // token), not the Linux one's. Documenting this (not asserting it's
    // fine) is the point: a merged SHA256SUMS.txt is a correctness hazard
    // release.yml must never produce again, not just a lookup miss.
    QCOMPARE(findChecksum(sums, QStringLiteral("TraceView-2.5.3-linux-x64.tar.gz")),
             QStringLiteral("abc123"));
}

}  // namespace

QTEST_MAIN(TestUpdateDownloader)
#include "test_updatedownloader.moc"
