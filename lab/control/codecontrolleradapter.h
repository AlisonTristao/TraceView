#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "lab/code/codemodule.h"
#include "lab/control/controlleradapter.h"

namespace tracelab {

// A controller written in C (lab/code, tracelab.h): ctl_step() runs once per
// loop.state with its TL_IN/TL_IN_ARRAY variables filled from the signals of
// the same names (the plant's measurements after the filters), its
// TL_PREVIEW from sim.preview, and its TL_OUT/TL_OUT_PLAN variables sent as
// the loop.u inputs of the same names -- every input the plant takes must be
// written. TL_DIAG values go to ctl.diag, tl_predicted() to its pred_*
// fields. A fault in the code stops the controller (the plant holds its
// input) until reset() or a new load().
class CodeControllerAdapter final : public ControllerAdapter {
public:
    CodeControllerAdapter();

    // Compiles `source` (shown as `fileName`). False when it does not compile
    // (module().messages()); the previous code is gone either way.
    bool load(const QString& source, const QString& fileName);
    code::CodeModule& module() {
        return m_module;
    }
    const code::CodeModule& module() const {
        return m_module;
    }
    // The plant's sample period (tl_ts()).
    void setSampleTime(double seconds);
    // Why the last compute() produced nothing although the code is loaded:
    // a fault ("crash.c:6: invalid memory access"), empty otherwise.
    QString runtimeErrorText() const;
    // True when the code reads TL_PREVIEW (information the robot may not
    // have, plan decision L5).
    bool usesPreview() const;

    QString name() const override;
    bool configure(int measurementCount, int inputCount, QString* error) override;
    void setSignalNames(const QStringList& measurements, const QStringList& inputs) override;
    void reset() override;
    QVector<ControllerParam> params() const override;
    void setParam(const QString& name, double value) override;
    QStringList diagNames() const override;
    QStringList diagUnits() const override;
    bool compute(const ControllerInput& input, ControllerOutput* output) override;

private:
    struct InBinding {
        const code::Declaration* decl;
        QVector<int> indices;  // into y[], one per element
    };
    struct OutBinding {
        const code::Declaration* decl = nullptr;  // null: input not written
    };

    code::CodeModule m_module;
    QStringList m_signals;
    QStringList m_inputs;
    QVector<InBinding> m_in;
    QVector<OutBinding> m_out;  // one per plant input, in loop.u order
    bool m_bound = false;
};

}  // namespace tracelab
