#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace tracelab {

// One stage between the plant's measurements and the controller, run by the
// ControllerHost on every loop.state (the place a filter has on the robot:
// in the firmware, before the control law). It works on named signals: it is
// told the names coming in and answers the names going out, so a stage can
// replace a signal (same name), add one (a new name) or pass it through.
// Called from the ControllerHost's thread, one call at a time.
class SignalFilter {
public:
    virtual ~SignalFilter() = default;

    // Shown in the controller's info line and the TraceLab tab.
    virtual QString name() const = 0;

    // `inputs`: the signals coming in, in order ("s[0]".."s[n-1]" for an
    // array). Fills `outputs` with the signals going out. False, with
    // `error`, when the stage cannot work with them (a name it reads is
    // missing).
    virtual bool configure(const QStringList& inputs, QStringList* outputs, QString* error) = 0;
    // Back to the initial internal state (sim.reset).
    virtual void reset() = 0;
    // One sample: `in` in configure()'s input order, `out` in its output
    // order. False when it produced nothing usable (a fault): the host then
    // holds the plant's input for this tick.
    virtual bool step(quint64 tick, double ts, const QVector<double>& in, QVector<double>* out) = 0;

    // Values published in ctl.diag after the controller's own, in order.
    virtual QStringList diagNames() const {
        return {};
    }
    virtual QStringList diagUnits() const {
        return {};
    }
    virtual QVector<double> diag() const {
        return {};
    }
};

}  // namespace tracelab
