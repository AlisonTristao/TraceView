#include "codecontrolleradapter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <cmath>

namespace tracelab {

using code::DeclKind;
using code::Declaration;

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("CodeControllerAdapter", text);
}

// The y[] positions of a scalar ("w_l") or array ("s[0]".."s[n-1]") signal;
// empty when the signals do not have it (all of it).
QVector<int> signalIndices(const QStringList& signalNames, const QString& name, int count,
                           bool array) {
    QVector<int> indices;
    if (!array) {
        const int index = signalNames.indexOf(name);
        if (index >= 0) {
            indices.append(index);
        }
        return indices;
    }
    for (int i = 0; i < count; ++i) {
        const int index = signalNames.indexOf(QStringLiteral("%1[%2]").arg(name).arg(i));
        if (index < 0) {
            return {};
        }
        indices.append(index);
    }
    return indices;
}

}  // namespace

CodeControllerAdapter::CodeControllerAdapter() = default;

bool CodeControllerAdapter::load(const QString& source, const QString& fileName) {
    m_in.clear();
    m_out.clear();
    m_bound = false;
    return m_module.compile(source, fileName);
}

void CodeControllerAdapter::setSampleTime(double seconds) {
    m_module.context().ts = seconds;
}

QString CodeControllerAdapter::runtimeErrorText() const {
    if (!m_module.hasRuntimeError()) {
        return QString();
    }
    const code::CodeMessage& error = m_module.runtimeError();
    const QString file = QFileInfo(m_module.fileName()).fileName();
    return error.line > 0 ? QStringLiteral("%1:%2: %3").arg(file).arg(error.line).arg(error.text)
                          : QStringLiteral("%1: %2").arg(file, error.text);
}

bool CodeControllerAdapter::usesPreview() const {
    return m_module.hasDeclaration(DeclKind::Preview);
}

QString CodeControllerAdapter::name() const {
    const QString file = QFileInfo(m_module.fileName()).completeBaseName();
    return file.isEmpty() ? tr("controller (code)") : file;
}

void CodeControllerAdapter::setSignalNames(const QStringList& measurements,
                                           const QStringList& inputs) {
    m_signals = measurements;
    m_inputs = inputs;
}

bool CodeControllerAdapter::configure(int measurementCount, int inputCount, QString* error) {
    Q_UNUSED(measurementCount);
    Q_UNUSED(inputCount);
    m_in.clear();
    m_out.clear();
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
    if (!m_module.hasFunction("ctl_step")) {
        return fail(tr("the code has no ctl_step()"));
    }
    for (const Declaration* decl : m_module.declarations(DeclKind::In)) {
        const QVector<int> indices =
            signalIndices(m_signals, decl->name, decl->count, decl->count > 1);
        if (indices.isEmpty()) {
            return fail(tr("%1 is not a signal of the plant (it has: %2)")
                            .arg(decl->name, m_signals.join(QStringLiteral(", "))));
        }
        m_in.append({decl, indices});
    }
    // Every input of the plant written, nothing else.
    for (const QString& input : std::as_const(m_inputs)) {
        m_out.append({m_module.find(DeclKind::Out, input)});
        if (!m_out.last().decl) {
            return fail(tr("the plant's input %1 is not written (TL_OUT(%1))").arg(input));
        }
    }
    for (const Declaration* decl : m_module.declarations(DeclKind::Out)) {
        if (!m_inputs.contains(decl->name)) {
            return fail(tr("%1 is not an input of the plant (it takes: %2)")
                            .arg(decl->name, m_inputs.join(QStringLiteral(", "))));
        }
    }
    m_bound = true;
    return true;
}

void CodeControllerAdapter::reset() {
    m_module.clearRuntimeError();
    m_module.context().restartNoise();
    m_module.context().predictedX.clear();
    m_module.context().predictedY.clear();
    m_module.context().predictedTheta.clear();
    if (m_module.hasFunction("ctl_reset")) {
        m_module.call("ctl_reset");
    }
}

QVector<ControllerParam> CodeControllerAdapter::params() const {
    QVector<ControllerParam> list;
    for (const Declaration* decl : m_module.params()) {
        list.append({decl->name, decl->unit, m_module.paramValue(decl->name), decl->minimum,
                     decl->maximum});
    }
    return list;
}

void CodeControllerAdapter::setParam(const QString& name, double value) {
    m_module.setParamValue(name, value);
}

QStringList CodeControllerAdapter::diagNames() const {
    QStringList names;
    for (const Declaration* decl : m_module.declarations(DeclKind::Diag)) {
        names.append(decl->name);
    }
    return names;
}

QStringList CodeControllerAdapter::diagUnits() const {
    QStringList units;
    for (const Declaration* decl : m_module.declarations(DeclKind::Diag)) {
        units.append(decl->unit);
    }
    return units;
}

bool CodeControllerAdapter::compute(const ControllerInput& input, ControllerOutput* output) {
    if (!m_bound || m_module.hasRuntimeError()) {
        return false;
    }
    for (const InBinding& in : std::as_const(m_in)) {
        double* values = in.decl->values();
        for (int e = 0; e < in.indices.size(); ++e) {
            values[e] = input.y.value(in.indices[e], qQNaN());
        }
    }
    int previewCount = int(input.reference.size() / 2);
    for (const Declaration* decl : m_module.declarations(DeclKind::Preview)) {
        if (previewCount == 0) {
            return false;  // no preview of this tick (the start): hold
        }
        const int count = qMin(previewCount, decl->count);
        double* x = decl->values();
        double* y = static_cast<double*>(decl->second);
        for (int i = 0; i < decl->count; ++i) {
            x[i] = i < count ? input.reference[i] : qQNaN();
            y[i] = i < count ? input.reference[previewCount + i] : qQNaN();
        }
        previewCount = count;
    }
    code::CodeContext& context = m_module.context();
    context.tick = input.tick;
    context.time = double(input.tick) * context.ts;
    context.previewCount = previewCount;
    context.predictedX.clear();
    context.predictedY.clear();
    context.predictedTheta.clear();
    if (!m_module.call("ctl_step")) {
        return false;
    }

    // The plan: as many steps as the longest TL_OUT_PLAN, a shorter one
    // holding its last value.
    int steps = 1;
    for (const OutBinding& out : std::as_const(m_out)) {
        steps = qMax(steps, out.decl->count);
    }
    output->u.clear();
    for (int j = 0; j < steps; ++j) {
        QVector<double> step;
        step.reserve(m_out.size());
        for (const OutBinding& out : std::as_const(m_out)) {
            step.append(out.decl->values()[qMin(j, out.decl->count - 1)]);
        }
        output->u.append(step);
    }
    output->diag.clear();
    for (const Declaration* decl : m_module.declarations(DeclKind::Diag)) {
        output->diag.append(*decl->values());
    }
    output->predictedX = context.predictedX;
    output->predictedY = context.predictedY;
    output->predictedTheta = context.predictedTheta;
    output->iterations = 0;
    return true;
}

}  // namespace tracelab
