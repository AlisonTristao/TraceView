// End-to-end check of the native crash handler: re-runs this same binary as a
// child that installs the handler and crashes on purpose, then looks at what
// the handler left on disk. Skipped where canCaptureStack() is false.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

#include "crash/crashhandler.h"

using namespace traceview;

namespace {
constexpr char kChildFlag[] = "--crash-child";
}

class TestCrashHandler : public QObject {
    Q_OBJECT

private slots:
    void crashWritesStackFile();
};

void TestCrashHandler::crashWritesStackFile() {
    if (!CrashHandler::canCaptureStack()) QSKIP("No native stack capture on this platform");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString crashDir = temp.filePath(QStringLiteral("crash/run"));

    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {QString::fromLatin1(kChildFlag), crashDir});
    QVERIFY(child.waitForFinished(30000));
    QVERIFY2(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0,
             "child was supposed to crash");

    QFile stack(QDir(crashDir).filePath(CrashHandler::stackFileName()));
    QVERIFY2(stack.exists(), qPrintable(stack.fileName()));
    QVERIFY(stack.open(QIODevice::ReadOnly));
    const QByteArray text = stack.readAll();
    QVERIFY2(text.size() > 50, text.constData());
#if defined(Q_OS_WIN)
    QVERIFY(QFile::exists(QDir(crashDir).filePath(CrashHandler::dumpFileName())));
#endif
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], kChildFlag) == 0) {
        QCoreApplication app(argc, argv);
        CrashHandler::install(QString::fromLocal8Bit(argv[2]));
        CrashHandler::forceCrash();
    }
    QCoreApplication app(argc, argv);
    TestCrashHandler test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_crashhandler.moc"
