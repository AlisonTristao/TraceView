#include "lab/sim/environment.h"

namespace tracelab::sim {

// To write (plan phase T7): "track" -> a TrackEnvironment reading the .tfg.
std::unique_ptr<Environment> createEnvironment(const EnvironmentConfig& config) {
    (void)config;
    return nullptr;
}

}  // namespace tracelab::sim
