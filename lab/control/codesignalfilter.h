#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "lab/code/codemodule.h"
#include "lab/control/signalfilter.h"

namespace tracelab {

// A filter written in C (lab/code, tracelab.h): filt_step() runs on every
// sample with its TL_IN/TL_INOUT variables filled from the signals of the
// same names. What comes out is every signal that came in, a TL_INOUT (or a
// TL_OUT of an existing name) replaced by the variable's value after the
// step, and each TL_OUT of a new name added at the end. A fault stops the
// filter (the plant holds its input) until reset() or a new load().
class CodeSignalFilter final : public SignalFilter {
public:
    CodeSignalFilter();

    bool load(const QString& source, const QString& fileName);
    code::CodeModule& module() {
        return m_module;
    }
    const code::CodeModule& module() const {
        return m_module;
    }
    QString runtimeErrorText() const;

    QString name() const override;
    bool configure(const QStringList& inputs, QStringList* outputs, QString* error) override;
    void reset() override;
    bool step(quint64 tick, double ts, const QVector<double>& in, QVector<double>* out) override;
    QStringList diagNames() const override;
    QStringList diagUnits() const override;
    QVector<double> diag() const override;

private:
    struct Binding {
        const code::Declaration* decl;
        QVector<int> indices;  // into the signals coming in (reads) or going out (writes)
    };

    code::CodeModule m_module;
    QVector<Binding> m_reads;   // TL_IN, TL_INOUT
    QVector<Binding> m_writes;  // TL_INOUT, TL_OUT
    int m_inputCount = 0;
    int m_outputCount = 0;
    bool m_bound = false;
};

}  // namespace tracelab
