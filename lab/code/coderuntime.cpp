#include "coderuntime.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>

#include "libtcc.h"

namespace tracelab::code {

namespace {

constexpr char kResourceRoot[] = ":/tracelab/code/runtime";
constexpr char kHelpersObject[] = "tracelab-helpers.o";
// What tcc's -bt mode links (tcc_add_support): only tcc_backtrace(), for
// code that calls it; the real one needs stdio.
constexpr char kBacktraceObject[] = "bt-log.o";
constexpr char kBacktraceStub[] = "int tcc_backtrace(const char *fmt, ...) { (void)fmt; return 0; }\n";

void collectError(void* opaque, const char* message) {
    auto* text = static_cast<QString*>(opaque);
    if (!text->isEmpty()) {
        *text += QLatin1Char('\n');
    }
    *text += QString::fromUtf8(message);
}

// One object out of tcc's own sources (C or assembler), compiled by tcc.
bool buildObject(const QString& directory, const QString& output, const QStringList& sources,
                 QString* error) {
    TCCState* state = tcc_new();
    if (!state) {
        *error = QStringLiteral("tcc_new() failed");
        return false;
    }
    QString messages;
    tcc_set_error_func(state, &messages, &collectError);
    const QByteArray dir = QDir::toNativeSeparators(directory).toLocal8Bit();
    tcc_set_lib_path(state, dir.constData());
    tcc_set_options(state, "-nostdlib -nostdinc");
    tcc_add_sysinclude_path(state, dir.constData());
    tcc_set_output_type(state, TCC_OUTPUT_OBJ);
    bool ok = true;
    for (const QString& source : sources) {
        const QByteArray path = QDir::toNativeSeparators(directory + QLatin1Char('/') + source)
                                    .toLocal8Bit();
        if (tcc_add_file(state, path.constData()) < 0) {
            ok = false;
            break;
        }
    }
    if (ok) {
        const QByteArray out =
            QDir::toNativeSeparators(directory + QLatin1Char('/') + output).toLocal8Bit();
        ok = tcc_output_file(state, out.constData()) == 0;
    }
    tcc_delete(state);
    if (!ok) {
        *error = QStringLiteral("building %1: %2").arg(output, messages);
    }
    return ok;
}

}  // namespace

CodeRuntime& CodeRuntime::instance() {
    static CodeRuntime runtime;
    return runtime;
}

bool CodeRuntime::ensureReady(QString* error) {
    QMutexLocker locker(&m_mutex);
    if (m_ready || !m_error.isEmpty()) {
        if (error) {
            *error = m_error;
        }
        return m_ready;
    }

    // The resources' names and contents name the folder.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArrayLiteral("tracelab-code-runtime/1"));
    QStringList files;
    QDirIterator it(QString::fromLatin1(kResourceRoot), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    files.sort();
    for (const QString& file : std::as_const(files)) {
        QFile in(file);
        if (in.open(QIODevice::ReadOnly)) {
            hash.addData(file.toUtf8());
            hash.addData(in.readAll());
        }
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/TraceView");
    }
    const QString directory = base + QStringLiteral("/tracelab-code/") +
                              QString::fromLatin1(hash.result().toHex().left(12));
    const QString stamp = directory + QStringLiteral("/.ready");

    if (!QFile::exists(stamp)) {
        if (!QDir().mkpath(directory)) {
            m_error = QStringLiteral("cannot create %1").arg(directory);
        }
        const QString root = QString::fromLatin1(kResourceRoot);
        for (const QString& file : std::as_const(files)) {
            if (!m_error.isEmpty()) {
                break;
            }
            const QString target = directory + file.mid(root.size());
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::remove(target);
            QFile in(file);
            QFile out(target);
            if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly) ||
                out.write(in.readAll()) < 0) {
                m_error = QStringLiteral("cannot write %1").arg(target);
            }
        }
        if (m_error.isEmpty()) {
            QFile stub(directory + QStringLiteral("/bt-log.c"));
            if (!stub.open(QIODevice::WriteOnly) || stub.write(kBacktraceStub) < 0) {
                m_error = QStringLiteral("cannot write %1").arg(stub.fileName());
            }
        }
        if (m_error.isEmpty()) {
            QStringList helpers = {QStringLiteral("libtcc1.c"), QStringLiteral("alloca.S")};
#ifdef Q_OS_WIN
            helpers.append(QStringLiteral("chkstk.S"));
#endif
            QString message;
            if (!buildObject(directory, QString::fromLatin1(kBacktraceObject),
                             {QStringLiteral("bt-log.c")}, &message) ||
                !buildObject(directory, QString::fromLatin1(kHelpersObject), helpers, &message)) {
                m_error = message;
            }
        }
        if (m_error.isEmpty()) {
            QFile ready(stamp);
            if (!ready.open(QIODevice::WriteOnly)) {
                m_error = QStringLiteral("cannot write %1").arg(stamp);
            }
        }
    }

    m_ready = m_error.isEmpty();
    if (m_ready) {
        m_directory = directory;
    }
    if (error) {
        *error = m_error;
    }
    return m_ready;
}

QString CodeRuntime::directory() const {
    QMutexLocker locker(&m_mutex);
    return m_directory;
}

QString CodeRuntime::helpersObject() const {
    QMutexLocker locker(&m_mutex);
    return m_directory + QLatin1Char('/') + QString::fromLatin1(kHelpersObject);
}

}  // namespace tracelab::code
