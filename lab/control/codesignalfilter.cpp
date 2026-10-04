#include "codesignalfilter.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace tracelab {

using code::DeclKind;
using code::Declaration;

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("CodeSignalFilter", text);
}

QStringList elementNames(const Declaration& decl) {
    if (decl.count <= 1) {
        return {decl.name};
    }
    QStringList names;
    for (int i = 0; i < decl.count; ++i) {
        names.append(QStringLiteral("%1[%2]").arg(decl.name).arg(i));
    }
    return names;
}

}  // namespace

CodeSignalFilter::CodeSignalFilter() = default;

bool CodeSignalFilter::load(const QString& source, const QString& fileName) {
    m_reads.clear();
    m_writes.clear();
    m_bound = false;
    return m_module.compile(source, fileName);
}

QString CodeSignalFilter::runtimeErrorText() const {
    if (!m_module.hasRuntimeError()) {
        return QString();
    }
    const code::CodeMessage& error = m_module.runtimeError();
    const QString file = QFileInfo(m_module.fileName()).fileName();
    return error.line > 0 ? QStringLiteral("%1:%2: %3").arg(file).arg(error.line).arg(error.text)
                          : QStringLiteral("%1: %2").arg(file, error.text);
}

QString CodeSignalFilter::name() const {
    const QString file = QFileInfo(m_module.fileName()).completeBaseName();
    return file.isEmpty() ? tr("filter (code)") : file;
}

bool CodeSignalFilter::configure(const QStringList& inputs, QStringList* outputs,
                                 QString* error) {
    m_reads.clear();
    m_writes.clear();
    m_bound = false;
    const auto fail = [error](const QString& text) {
        if (error) {
            *error = text;
        }
        return false;
    };
    if (!m_module.isCompiled()) {
        return fail(m_module.errorText().isEmpty() ? tr("no code loaded") : m_module.errorText());
    }
    if (!m_module.hasFunction("filt_step")) {
        return fail(tr("the code has no filt_step()"));
    }
    QStringList out = inputs;
    for (const Declaration& decl : m_module.declarations()) {
        const bool reads = decl.kind == DeclKind::In || decl.kind == DeclKind::InOut;
        const bool writes = decl.kind == DeclKind::Out || decl.kind == DeclKind::InOut;
        if (!reads && !writes) {
            continue;
        }
        const QStringList names = elementNames(decl);
        if (reads) {
            Binding binding{&decl, {}};
            for (const QString& name : names) {
                const int index = inputs.indexOf(name);
                if (index < 0) {
                    return fail(tr("%1 is not a signal here (there are: %2)")
                                    .arg(decl.name, inputs.join(QStringLiteral(", "))));
                }
                binding.indices.append(index);
            }
            m_reads.append(binding);
        }
        if (writes) {
            Binding binding{&decl, {}};
            for (const QString& name : names) {
                int index = out.indexOf(name);
                if (index < 0) {
                    out.append(name);  // a new signal
                    index = int(out.size()) - 1;
                }
                binding.indices.append(index);
            }
            m_writes.append(binding);
        }
    }
    m_inputCount = int(inputs.size());
    m_outputCount = int(out.size());
    if (outputs) {
        *outputs = out;
    }
    m_bound = true;
    return true;
}

void CodeSignalFilter::reset() {
    m_module.clearRuntimeError();
    m_module.context().restartNoise();
    if (m_module.hasFunction("filt_reset")) {
        m_module.call("filt_reset");
    }
}

bool CodeSignalFilter::step(quint64 tick, double ts, const QVector<double>& in,
                            QVector<double>* out) {
    if (!m_bound || m_module.hasRuntimeError()) {
        return false;
    }
    for (const Binding& read : std::as_const(m_reads)) {
        double* values = read.decl->values();
        for (int e = 0; e < read.indices.size(); ++e) {
            values[e] = in.value(read.indices[e], qQNaN());
        }
    }
    code::CodeContext& context = m_module.context();
    context.ts = ts;
    context.tick = tick;
    context.time = double(tick) * ts;
    if (!m_module.call("filt_step")) {
        return false;
    }
    *out = in;
    out->resize(m_outputCount);
    for (int i = m_inputCount; i < m_outputCount; ++i) {
        (*out)[i] = qQNaN();
    }
    for (const Binding& write : std::as_const(m_writes)) {
        const double* values = write.decl->values();
        for (int e = 0; e < write.indices.size(); ++e) {
            (*out)[write.indices[e]] = values[e];
        }
    }
    return true;
}

QStringList CodeSignalFilter::diagNames() const {
    QStringList names;
    for (const Declaration* decl : m_module.declarations(DeclKind::Diag)) {
        names.append(decl->name);
    }
    return names;
}

QStringList CodeSignalFilter::diagUnits() const {
    QStringList units;
    for (const Declaration* decl : m_module.declarations(DeclKind::Diag)) {
        units.append(decl->unit);
    }
    return units;
}

QVector<double> CodeSignalFilter::diag() const {
    QVector<double> values;
    for (const Declaration* decl : m_module.declarations(DeclKind::Diag)) {
        values.append(*decl->values());
    }
    return values;
}

}  // namespace tracelab
