#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace tracelab {

// A live-editable controller parameter (gain, horizon weight...), shown with
// its name, unit and range by the controller panel.
struct ControllerParam {
    QString name;
    QString unit;
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
};

// What the controller is given at one tick. Positions, not names: the
// ControllerHost has already translated the names of loop.state's fields
// into y[] (declaration order) and will translate u[] back into loop.u's
// inputs, so an adapter never parses BTP.
struct ControllerInput {
    quint64 tick = 0;
    QVector<double> y;
    // sim.preview of the same tick, when the plant publishes one: the
    // previewCount car-frame points as px[0..n), then py[0..n). Empty without.
    QVector<double> reference;
};

// What it answers. `u[j]` is the input vector for tick + j (j = 0..M-1),
// each in loop.u's input order -- M = 1 for a PD, the whole horizon for an
// MPC. `diag` holds one value per diagNames() entry. `predictedX`/
// `predictedY` are the predicted path in the car frame of this tick (x
// lateral right, y forward, m), empty when the controller has none.
struct ControllerOutput {
    QVector<QVector<double>> u;
    QVector<double> diag;
    QVector<double> predictedX;
    QVector<double> predictedY;
    int iterations = 0;
};

// One way of programming a controller (plan 5.2): a loopctl DLL, a script,
// and -- not an adapter, since it speaks BTP itself -- an external process.
// The ControllerHost calls these from its own thread, one call at a time.
class ControllerAdapter {
public:
    virtual ~ControllerAdapter() = default;

    // Shown in the info line of the scene (ctl.diag) and the controller panel.
    virtual QString name() const = 0;

    // Sizes taken from the plant's manifest: y[] and u[] widths. False, with
    // `error`, when the controller cannot work with them.
    virtual bool configure(int measurementCount, int inputCount, QString* error) = 0;
    // Back to the initial internal state (sim.reset).
    virtual void reset() = 0;

    virtual QVector<ControllerParam> params() const = 0;
    virtual void setParam(const QString& name, double value) = 0;
    // Names (and units, same order) of the values in ControllerOutput::diag.
    virtual QStringList diagNames() const = 0;
    virtual QStringList diagUnits() const = 0;

    // One control step. False when it produced nothing usable this tick
    // (the host then sends no loop.u, and the plant holds its input).
    virtual bool compute(const ControllerInput& input, ControllerOutput* output) = 0;
};

}  // namespace tracelab
