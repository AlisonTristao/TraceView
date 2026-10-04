#include "codemodule.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "lab/code/codecall.h"
#include "lab/code/coderuntime.h"
#include "libtcc.h"

namespace tracelab::code {

namespace {

constexpr char kDeclPrefix[] = "tl__decl_";

// Makes the context current for the duration of a call.
class ContextScope {
public:
    explicit ContextScope(CodeContext* context) : m_previous(currentContext()) {
        currentContext() = context;
    }
    ~ContextScope() {
        currentContext() = m_previous;
    }

private:
    CodeContext* m_previous;
};

}  // namespace

CodeModule::CodeModule() = default;

CodeModule::~CodeModule() {
    release();
}

void CodeModule::release() {
    if (m_state) {
        tcc_delete(m_state);
        m_state = nullptr;
    }
    m_declarations.clear();
    m_functions.clear();
}

void CodeModule::onCompilerMessage(void* opaque, const char* message) {
    auto* self = static_cast<CodeModule*>(opaque);
    const QString text = QString::fromUtf8(message);
    // "file:12: error: text", "file:12: warning: text", "tcc: error: text"
    static const QRegularExpression located(QStringLiteral("^(.*):(\\d+): (error|warning): (.*)$"));
    static const QRegularExpression bare(QStringLiteral("^(?:tcc: )?(error|warning): (.*)$"));
    CodeMessage entry;
    QRegularExpressionMatch match = located.match(text);
    if (match.hasMatch()) {
        entry.line = match.captured(2).toInt();
        entry.warning = match.captured(3) == QLatin1String("warning");
        entry.text = match.captured(4);
    } else if ((match = bare.match(text)).hasMatch()) {
        entry.warning = match.captured(1) == QLatin1String("warning");
        entry.text = match.captured(2);
    } else {
        entry.text = text;  // "In file included from ..." and the like
        entry.warning = true;
    }
    self->m_messages.append(entry);
}

int CodeModule::onBacktrace(void* opaque, void* pc, const char* file, int line,
                            const char* function, const char* message) {
    Q_UNUSED(pc);
    Q_UNUSED(file);
    auto* self = static_cast<CodeModule*>(opaque);
    if (self->m_runtimeError.text.isEmpty()) {
        QString text = QString::fromUtf8(message ? message : "");
        text.remove(QStringLiteral("RUNTIME ERROR: "));
        if (function && *function) {
            text = QStringLiteral("%1 (in %2)").arg(text, QString::fromUtf8(function));
        }
        self->m_runtimeError.text = text.trimmed();
        self->m_runtimeError.line = line;
    }
    return 0;  // the innermost frame is enough
}

bool CodeModule::compile(const QString& source, const QString& fileName,
                         const QVector<HostSymbol>& extraSymbols) {
    release();
    m_messages.clear();
    m_runtimeError = {};
    m_fileName = fileName;

    QString runtimeError;
    if (!CodeRuntime::instance().ensureReady(&runtimeError)) {
        m_messages.append({0, QStringLiteral("TraceLab's C runtime is unavailable: %1").arg(runtimeError),
                           false});
        return false;
    }
    const QByteArray directory =
        QDir::toNativeSeparators(CodeRuntime::instance().directory()).toLocal8Bit();
    const QByteArray helpers =
        QDir::toNativeSeparators(CodeRuntime::instance().helpersObject()).toLocal8Bit();

    TCCState* state = tcc_new();
    if (!state) {
        m_messages.append({0, QStringLiteral("cannot start the compiler"), false});
        return false;
    }
    tcc_set_error_func(state, this, &CodeModule::onCompilerMessage);
    tcc_set_lib_path(state, directory.constData());
    // -bt: line tables, so a fault reports its line (and tcc_setjmp works).
    tcc_set_options(state, "-nostdlib -nostdinc -bt -Wall");
    tcc_add_sysinclude_path(state, directory.constData());
    tcc_add_library_path(state, directory.constData());
    tcc_set_output_type(state, TCC_OUTPUT_MEMORY);

    // Messages and faults name the file and its own line numbers.
    QString name = fileName.isEmpty() ? QStringLiteral("block.c") : QFileInfo(fileName).fileName();
    name.replace(QLatin1Char('"'), QLatin1Char('_'));
    const QByteArray text =
        QStringLiteral("#line 1 \"%1\"\n").arg(name).toUtf8() + source.toUtf8() + '\n';
    bool ok = tcc_compile_string(state, text.constData()) == 0 &&
              tcc_add_file(state, helpers.constData()) == 0;
    if (ok) {
        addHostSymbols(state);
        for (const HostSymbol& symbol : extraSymbols) {
            tcc_add_symbol(state, symbol.name.constData(), symbol.address);
        }
        ok = tcc_relocate(state) == 0;
    }
    if (!ok) {
        tcc_delete(state);
        if (std::none_of(m_messages.cbegin(), m_messages.cend(),
                         [](const CodeMessage& m) { return !m.warning; })) {
            m_messages.append({0, QStringLiteral("the code does not compile"), false});
        }
        return false;
    }
    m_state = state;
    tcc_set_backtrace_func(state, this, &CodeModule::onBacktrace);

    // The declarations, back from their tl__decl_* symbols, in the order they
    // sit in memory -- the order of the source.
    struct Found {
        const TlDecl* decl;
    };
    QVector<Found> found;
    tcc_list_symbols(state, &found, [](void* ctx, const char* symbol, const void* value) {
        if (std::strncmp(symbol, kDeclPrefix, sizeof kDeclPrefix - 1) == 0 && value) {
            static_cast<QVector<Found>*>(ctx)->append({static_cast<const TlDecl*>(value)});
        }
    });
    std::sort(found.begin(), found.end(),
              [](const Found& a, const Found& b) { return a.decl < b.decl; });
    for (const Found& f : std::as_const(found)) {
        Declaration d;
        d.kind = DeclKind(f.decl->kind);
        d.name = QString::fromUtf8(f.decl->name ? f.decl->name : "");
        d.unit = QString::fromUtf8(f.decl->unit ? f.decl->unit : "");
        if (f.decl->options && *f.decl->options) {
            d.options = QString::fromUtf8(f.decl->options).split(QLatin1Char('|'));
        }
        d.defaultValue = f.decl->def;
        d.minimum = f.decl->lo;
        d.maximum = f.decl->hi;
        d.count = qMax(1, f.decl->count);
        d.variable = f.decl->ptr;
        d.second = f.decl->ptr2;
        if (d.kind == DeclKind::ParamEnum) {
            d.minimum = 0;
            d.maximum = qMax(0, int(d.options.size()) - 1);
        }
        m_declarations.append(d);
    }
    for (const char* function :
         {"plant_reset", "plant_deriv", "plant_update", "plant_output", "filt_reset", "filt_step",
          "ctl_reset", "ctl_step"}) {
        if (void* address = tcc_get_symbol(state, function)) {
            m_functions.insert(function, reinterpret_cast<void (*)()>(address));
        }
    }
    // The values set before, onto the new code.
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
        for (const Declaration& d : std::as_const(m_declarations)) {
            if (d.isParam() && d.name == it.key()) {
                applyOverride(d, it.value().toDouble());
            }
        }
    }
    return true;
}

QString CodeModule::errorText() const {
    for (const CodeMessage& m : m_messages) {
        if (!m.warning) {
            return m.line > 0 ? QStringLiteral("%1:%2: %3")
                                    .arg(QFileInfo(m_fileName).fileName())
                                    .arg(m.line)
                                    .arg(m.text)
                              : m.text;
        }
    }
    return QString();
}

QVector<const Declaration*> CodeModule::declarations(DeclKind kind) const {
    QVector<const Declaration*> list;
    for (const Declaration& d : m_declarations) {
        if (d.kind == kind) {
            list.append(&d);
        }
    }
    return list;
}

const Declaration* CodeModule::find(DeclKind kind, const QString& name) const {
    for (const Declaration& d : m_declarations) {
        if (d.kind == kind && d.name == name) {
            return &d;
        }
    }
    return nullptr;
}

bool CodeModule::hasDeclaration(DeclKind kind) const {
    return std::any_of(m_declarations.cbegin(), m_declarations.cend(),
                       [kind](const Declaration& d) { return d.kind == kind; });
}

bool CodeModule::hasFunction(const char* name) const {
    return m_functions.contains(name);
}

bool CodeModule::call(const char* name) {
    const auto function = m_functions.value(name);
    if (!m_state || !function || hasRuntimeError()) {
        return false;
    }
    ContextScope scope(&m_context);
    const int code = tracelab_guarded_call(m_state, function);
    if (code != 0) {
        if (m_runtimeError.text.isEmpty()) {
            m_runtimeError.text = QStringLiteral("the code faulted in %1()").arg(QLatin1String(name));
        }
        return false;
    }
    return true;
}

void CodeModule::clearRuntimeError() {
    m_runtimeError = {};
}

QVector<const Declaration*> CodeModule::params() const {
    QVector<const Declaration*> list;
    for (const Declaration& d : m_declarations) {
        if (d.isParam()) {
            list.append(&d);
        }
    }
    return list;
}

double CodeModule::paramValue(const QString& name) const {
    for (const Declaration& d : m_declarations) {
        if (d.isParam() && d.name == name && d.variable) {
            return d.isIntParam() ? double(*static_cast<int*>(d.variable)) : *d.values();
        }
    }
    return m_overrides.value(name).toDouble(qQNaN());
}

void CodeModule::applyOverride(const Declaration& d, double value) {
    if (!d.variable || std::isnan(value)) {
        return;
    }
    // A range of [0, 0] (or inverted) means none was given.
    if (d.maximum > d.minimum) {
        value = qBound(d.minimum, value, d.maximum);
    }
    if (d.isIntParam()) {
        *static_cast<int*>(d.variable) = int(std::lround(value));
    } else {
        *d.values() = value;
    }
}

bool CodeModule::setParamValue(const QString& name, double value) {
    for (const Declaration& d : std::as_const(m_declarations)) {
        if (d.isParam() && d.name == name) {
            applyOverride(d, value);
            m_overrides.insert(name, paramValue(name));
            return true;
        }
    }
    return false;
}

QJsonObject CodeModule::paramOverrides() const {
    return m_overrides;
}

void CodeModule::setParamOverrides(const QJsonObject& values) {
    m_overrides = values;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        for (const Declaration& d : std::as_const(m_declarations)) {
            if (d.isParam() && d.name == it.key()) {
                applyOverride(d, it.value().toDouble());
            }
        }
    }
}

QStringList CodeModule::takeLog() {
    QStringList lines = m_context.log;
    m_context.log.clear();
    return lines;
}

}  // namespace tracelab::code
