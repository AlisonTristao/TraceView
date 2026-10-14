#pragma once

#include "modules/appmodule.h"

namespace tracelab {

// TraceLab as a TraceView module (docs/MODULES.md): id "lab", so it is
// switched on in Settings > General > Modules or for one run with
// `TraceView --mode lab` (the TraceLab launcher). install() registers the
// scenes, the Scene dashboard widget and the "TraceLab" ribbon tab.
class LabModule final : public traceview::AppModule {
public:
    static constexpr char kId[] = "lab";

    QString id() const override;
    QString displayName() const override;
    QString description() const override;
    void install(traceview::ModuleHost& host) override;
};

}  // namespace tracelab
