#include "codeplant.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <cmath>

namespace tracelab {

using code::DeclKind;
using code::Declaration;

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("CodePlant", text);
}

}  // namespace

CodePlant::CodePlant() = default;

bool CodePlant::load(const QString& source, const QString& fileName,
                     const QVector<code::HostSymbol>& symbols) {
    m_loaded = false;
    m_error.clear();
    m_inputs.clear();
    m_states.clear();
    m_outputs.clear();
    m_diags.clear();
    if (!m_module.compile(source, fileName, symbols)) {
        m_error = m_module.errorText();
        return false;
    }
    m_discrete = m_module.hasDeclaration(DeclKind::Discrete);
    if (m_discrete && !m_module.hasFunction("plant_update")) {
        m_error = tr("a TL_DISCRETE plant needs plant_update() (writing next(x))");
        return false;
    }
    if (!m_discrete && !m_module.hasFunction("plant_deriv") &&
        !m_module.declarations(DeclKind::State).isEmpty()) {
        m_error = tr("a continuous plant needs plant_deriv() (writing der(x)); add TL_DISCRETE "
                     "for plant_update()");
        return false;
    }
    m_inputs = m_module.declarations(DeclKind::Input);
    m_states = m_module.declarations(DeclKind::State);
    m_outputs = m_module.declarations(DeclKind::Output);
    m_diags = m_module.declarations(DeclKind::Diag);
    if (m_outputs.isEmpty()) {
        m_error = tr("the plant has no TL_OUTPUT: nothing for a controller to read");
        return false;
    }
    m_loaded = true;
    return true;
}

void CodePlant::setSolver(Solver solver, int substeps) {
    m_solver = solver;
    m_substeps = qBound(1, substeps, 1000);
}

void CodePlant::setDisturbances(const QVector<double>& values) {
    m_module.context().disturbances = values;
}

QString CodePlant::runtimeErrorText() const {
    if (!m_module.hasRuntimeError()) {
        return QString();
    }
    const code::CodeMessage& error = m_module.runtimeError();
    const QString file = QFileInfo(m_module.fileName()).fileName();
    return error.line > 0 ? QStringLiteral("%1:%2: %3").arg(file).arg(error.line).arg(error.text)
                          : QStringLiteral("%1: %2").arg(file, error.text);
}

bool CodePlant::reset(const QHash<QString, double>& initial) {
    if (!m_loaded) {
        return false;
    }
    m_module.clearRuntimeError();
    code::CodeContext& context = m_module.context();
    context.restartNoise();
    context.ts = m_ts;
    context.tick = 0;
    context.time = 0.0;
    m_tick = 0;
    m_time = 0.0;
    for (const Declaration* state : std::as_const(m_states)) {
        const double x0 = initial.value(state->name, state->defaultValue);
        *state->values() = x0;
        *static_cast<double*>(state->second) = x0;
    }
    for (const Declaration* input : std::as_const(m_inputs)) {
        *input->values() = 0.0;
    }
    if (m_module.hasFunction("plant_reset") && !m_module.call("plant_reset")) {
        return false;
    }
    return !m_module.hasFunction("plant_output") || m_module.call("plant_output");
}

void CodePlant::writeStates(const QVector<double>& x) {
    for (int i = 0; i < m_states.size(); ++i) {
        *m_states[i]->values() = x[i];
    }
}

bool CodePlant::derivatives(const QVector<double>& x, QVector<double>* dx) {
    writeStates(x);
    if (!m_module.call("plant_deriv")) {
        return false;
    }
    dx->resize(m_states.size());
    for (int i = 0; i < m_states.size(); ++i) {
        (*dx)[i] = *static_cast<double*>(m_states[i]->second);
    }
    return true;
}

bool CodePlant::step(const QVector<double>& u) {
    if (!m_loaded || m_module.hasRuntimeError()) {
        return false;
    }
    // The inputs, held over the period, within their bounds.
    for (int i = 0; i < m_inputs.size(); ++i) {
        double value = u.value(i, 0.0);
        const Declaration* input = m_inputs[i];
        if (input->maximum > input->minimum) {
            value = qBound(input->minimum, value, input->maximum);
        }
        *input->values() = value;
    }
    code::CodeContext& context = m_module.context();
    context.ts = m_ts;
    context.tick = quint64(m_tick);
    context.time = m_time;

    if (m_discrete) {
        if (!m_module.call("plant_update")) {
            return false;
        }
        for (const Declaration* state : std::as_const(m_states)) {
            *state->values() = *static_cast<double*>(state->second);
        }
    } else if (!m_states.isEmpty()) {
        QVector<double> x(m_states.size());
        for (int i = 0; i < m_states.size(); ++i) {
            x[i] = *m_states[i]->values();
        }
        const double h = m_ts / m_substeps;
        QVector<double> k1, k2, k3, k4, tmp(x.size());
        for (int s = 0; s < m_substeps; ++s) {
            context.time = m_time + s * h;
            if (!derivatives(x, &k1)) {
                return false;
            }
            if (m_solver == Solver::Euler) {
                for (int i = 0; i < x.size(); ++i) {
                    x[i] += h * k1[i];
                }
                continue;
            }
            for (int i = 0; i < x.size(); ++i) {
                tmp[i] = x[i] + 0.5 * h * k1[i];
            }
            context.time = m_time + s * h + 0.5 * h;
            if (!derivatives(tmp, &k2)) {
                return false;
            }
            for (int i = 0; i < x.size(); ++i) {
                tmp[i] = x[i] + 0.5 * h * k2[i];
            }
            if (!derivatives(tmp, &k3)) {
                return false;
            }
            for (int i = 0; i < x.size(); ++i) {
                tmp[i] = x[i] + h * k3[i];
            }
            context.time = m_time + (s + 1) * h;
            if (!derivatives(tmp, &k4)) {
                return false;
            }
            for (int i = 0; i < x.size(); ++i) {
                x[i] += h / 6.0 * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
            }
        }
        writeStates(x);
    }
    ++m_tick;
    m_time += m_ts;
    context.tick = quint64(m_tick);
    context.time = m_time;
    return !m_module.hasFunction("plant_output") || m_module.call("plant_output");
}

QStringList CodePlant::inputNames() const {
    QStringList names;
    for (const Declaration* d : m_inputs) {
        names.append(d->name);
    }
    return names;
}

QStringList CodePlant::outputNames() const {
    QStringList names;
    for (const Declaration* d : m_outputs) {
        if (d->count <= 1) {
            names.append(d->name);
        } else {
            for (int e = 0; e < d->count; ++e) {
                names.append(QStringLiteral("%1[%2]").arg(d->name).arg(e));
            }
        }
    }
    return names;
}

QStringList CodePlant::stateNames() const {
    QStringList names;
    for (const Declaration* d : m_states) {
        names.append(d->name);
    }
    return names;
}

QStringList CodePlant::diagNames() const {
    QStringList names;
    for (const Declaration* d : m_diags) {
        names.append(d->name);
    }
    return names;
}

QVector<double> CodePlant::outputs() const {
    QVector<double> values;
    for (const Declaration* d : m_outputs) {
        for (int e = 0; e < d->count; ++e) {
            values.append(d->values()[e]);
        }
    }
    return values;
}

QVector<double> CodePlant::states() const {
    QVector<double> values;
    for (const Declaration* d : m_states) {
        values.append(*d->values());
    }
    return values;
}

QVector<double> CodePlant::diags() const {
    QVector<double> values;
    for (const Declaration* d : m_diags) {
        values.append(*d->values());
    }
    return values;
}

QVector<double> CodePlant::appliedInputs() const {
    QVector<double> values;
    for (const Declaration* d : m_inputs) {
        values.append(*d->values());
    }
    return values;
}

double CodePlant::state(const QString& name) const {
    for (const Declaration* d : m_states) {
        if (d->name == name) {
            return *d->values();
        }
    }
    return qQNaN();
}

bool CodePlant::hasState(const QString& name) const {
    for (const Declaration* d : m_states) {
        if (d->name == name) {
            return true;
        }
    }
    return false;
}

}  // namespace tracelab
