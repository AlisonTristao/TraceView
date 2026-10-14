#include "dllcontrolleradapter.h"

#include <QFileInfo>

namespace tracelab {

// To write (plan phase T3): QLibrary + the loopctl.h entry points, a file
// watcher for the reload, and the diagnostics extension.
DllControllerAdapter::DllControllerAdapter(const QString& libraryPath)
    : m_libraryPath(libraryPath) {}

QString DllControllerAdapter::libraryPath() const {
    return m_libraryPath;
}

QString DllControllerAdapter::name() const {
    return QFileInfo(m_libraryPath).completeBaseName();
}

bool DllControllerAdapter::configure(int measurementCount, int inputCount, QString* error) {
    Q_UNUSED(measurementCount);
    Q_UNUSED(inputCount);
    if (error) {
        error->clear();
    }
    return false;
}

void DllControllerAdapter::reset() {}

QVector<ControllerParam> DllControllerAdapter::params() const {
    return {};
}

void DllControllerAdapter::setParam(const QString& name, double value) {
    Q_UNUSED(name);
    Q_UNUSED(value);
}

QStringList DllControllerAdapter::diagNames() const {
    return {};
}

QStringList DllControllerAdapter::diagUnits() const {
    return {};
}

bool DllControllerAdapter::compute(const ControllerInput& input, ControllerOutput* output) {
    Q_UNUSED(input);
    Q_UNUSED(output);
    return false;
}

}  // namespace tracelab
