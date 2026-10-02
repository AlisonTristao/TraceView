#include "scriptcontrolleradapter.h"

namespace tracelab {

// To write (plan phase T3): evaluate m_source in m_engine, then call its
// step()/params()/diag()/reset() as described in the header.
ScriptControllerAdapter::ScriptControllerAdapter(const QString& source) : m_source(source) {}

QString ScriptControllerAdapter::source() const {
    return m_source;
}

QString ScriptControllerAdapter::name() const {
    return QStringLiteral("script");
}

bool ScriptControllerAdapter::configure(int measurementCount, int inputCount, QString* error) {
    Q_UNUSED(measurementCount);
    Q_UNUSED(inputCount);
    if (error) {
        error->clear();
    }
    return false;
}

void ScriptControllerAdapter::reset() {}

QVector<ControllerParam> ScriptControllerAdapter::params() const {
    return {};
}

void ScriptControllerAdapter::setParam(const QString& name, double value) {
    Q_UNUSED(name);
    Q_UNUSED(value);
}

QStringList ScriptControllerAdapter::diagNames() const {
    return {};
}

QStringList ScriptControllerAdapter::diagUnits() const {
    return {};
}

bool ScriptControllerAdapter::compute(const ControllerInput& input, ControllerOutput* output) {
    Q_UNUSED(input);
    Q_UNUSED(output);
    return false;
}

}  // namespace tracelab
