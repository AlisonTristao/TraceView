#pragma once

#include <QString>

#include "lab/control/controlleradapter.h"

namespace tracelab {

// A controller compiled as a shared library against the loopctl C interface
// (btp/profiles/loopctl.h, BTP >= 2.50 -- not in the 2.48 TraceView pins
// yet), so the same source also builds into the firmware (plan stage 3).
// Reloaded when the file changes on disk (rebuilt). Plain C across the
// boundary, so the library's compiler does not have to match TraceView's.
// The optional diagnostics extension of loopctl.h feeds diagNames()/diag.
class DllControllerAdapter final : public ControllerAdapter {
public:
    explicit DllControllerAdapter(const QString& libraryPath);

    QString libraryPath() const;

    QString name() const override;
    bool configure(int measurementCount, int inputCount, QString* error) override;
    void reset() override;
    QVector<ControllerParam> params() const override;
    void setParam(const QString& name, double value) override;
    QStringList diagNames() const override;
    QStringList diagUnits() const override;
    bool compute(const ControllerInput& input, ControllerOutput* output) override;

private:
    QString m_libraryPath;
};

}  // namespace tracelab
