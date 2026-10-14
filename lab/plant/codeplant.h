#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "lab/code/codemodule.h"

namespace tracelab {

// A plant written in C (lab/code, tracelab.h): its TL_INPUTs held over each
// sample period (zero-order hold), its TL_STATEs advanced by plant_update()
// (TL_DISCRETE: next(x)) or by integrating plant_deriv() (der(x)) with a
// fixed-step solver, then plant_output() filling its TL_OUTPUTs -- what a
// real robot's sensors would report. Knows nothing of BTP: CodePlantDevice
// runs it on a clock and publishes it.
class CodePlant {
public:
    enum class Solver { Euler, Rk4 };

    CodePlant();

    // Compiles the plant; false when it does not compile (module().messages())
    // or misses its functions (errorText()). `symbols`: functions of an
    // environment the code may call, added before linking.
    bool load(const QString& source, const QString& fileName,
              const QVector<code::HostSymbol>& symbols = {});
    code::CodeModule& module() {
        return m_module;
    }
    const code::CodeModule& module() const {
        return m_module;
    }
    // Why load() refused code that compiled (no plant_update/plant_deriv...).
    const QString& errorText() const {
        return m_error;
    }
    bool isLoaded() const {
        return m_loaded;
    }
    bool isDiscrete() const {
        return m_discrete;
    }

    void setSampleTime(double seconds) {
        m_ts = seconds;
    }
    double sampleTime() const {
        return m_ts;
    }
    // Continuous plants only: the method and its steps per sample period.
    void setSolver(Solver solver, int substeps);
    Solver solver() const {
        return m_solver;
    }
    int substeps() const {
        return m_substeps;
    }

    // States back to x0 (or to `initial`'s value, by name: an environment
    // placing the plant), time to 0, plant_reset(), then plant_output() so
    // the outputs are valid at tick 0.
    bool reset(const QHash<QString, double>& initial = {});
    // One sample period with `u` (one per input, saturated to their bounds)
    // held over it. False when the code faulted.
    bool step(const QVector<double>& u);
    long long tick() const {
        return m_tick;
    }
    double time() const {
        return m_time;
    }

    // Names in declaration order; arrays expanded ("s[0]"...) for outputs.
    QStringList inputNames() const;
    QStringList outputNames() const;
    QStringList stateNames() const;
    QStringList diagNames() const;
    // Values in the same orders.
    QVector<double> outputs() const;
    QVector<double> states() const;
    QVector<double> diags() const;
    QVector<double> appliedInputs() const;
    // A state by name (NaN when there is none).
    double state(const QString& name) const;
    bool hasState(const QString& name) const;

    // What tl_disturb(i) returns to the code: disturbances the simulation
    // injects (sim.disturb), by channel; 0 beyond the ones set.
    void setDisturbances(const QVector<double>& values);

    // "file:line: text" of a fault, or empty.
    QString runtimeErrorText() const;

private:
    bool derivatives(const QVector<double>& x, QVector<double>* dx);
    void writeStates(const QVector<double>& x);

    code::CodeModule m_module;
    bool m_loaded = false;
    bool m_discrete = false;
    QString m_error;
    double m_ts = 1.0 / 80.0;
    Solver m_solver = Solver::Rk4;
    int m_substeps = 1;
    long long m_tick = 0;
    double m_time = 0.0;
    QVector<const code::Declaration*> m_inputs;
    QVector<const code::Declaration*> m_states;
    QVector<const code::Declaration*> m_outputs;
    QVector<const code::Declaration*> m_diags;
};

}  // namespace tracelab
