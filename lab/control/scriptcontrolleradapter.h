#pragma once

#include <QJSEngine>
#include <QString>

#include "lab/control/controlleradapter.h"

namespace tracelab {

// A quick controller written in JavaScript -- a PD, a state machine, gain
// scheduling -- run by the same QJSEngine family as TraceView's device
// scripts. Not for heavy optimisation: an MPC is a DLL or an external process.
//
// The script defines step(tick, y, ref, p) returning either an array of
// inputs (M = 1) or {u: [[...], ...], diag: [...]}, plus optional params()
// (array of {name, unit, value, min, max}), diag() (array of names) and
// reset(). `p` is an object of the current parameter values by name.
class ScriptControllerAdapter final : public ControllerAdapter {
public:
    explicit ScriptControllerAdapter(const QString& source);

    QString source() const;

    QString name() const override;
    bool configure(int measurementCount, int inputCount, QString* error) override;
    void reset() override;
    QVector<ControllerParam> params() const override;
    void setParam(const QString& name, double value) override;
    QStringList diagNames() const override;
    QStringList diagUnits() const override;
    bool compute(const ControllerInput& input, ControllerOutput* output) override;

private:
    QString m_source;
    QJSEngine m_engine;
};

}  // namespace tracelab
