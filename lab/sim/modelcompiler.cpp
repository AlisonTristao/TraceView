#include "lab/sim/odesystem.h"

namespace tracelab::sim {

// To write: parse every equation and output with ExprTk (kept to this one
// translation unit: it is a large header), bind the parameters, inputs,
// states and every Environment function by name, check that each state has
// exactly one equation, and wrap the result in an OdeSystem.
std::unique_ptr<OdeSystem> compileModel(const ModelDefinition& model,
                                        const std::vector<Environment*>& environments,
                                        std::string* error) {
    (void)model;
    (void)environments;
    if (error) {
        *error = "the model compiler is not implemented yet";
    }
    return nullptr;
}

}  // namespace tracelab::sim
