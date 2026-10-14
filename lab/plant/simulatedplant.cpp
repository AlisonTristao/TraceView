#include "simulatedplant.h"

#include "core/transport.h"

namespace tracelab {

SimulatedPlant::SimulatedPlant(QObject* parent) : QObject(parent) {}

SimulatedPlant::~SimulatedPlant() = default;

// To write (plan phase T2) from here down: compileModel() + createEnvironment()
// + createSolver() + createScheduler() in load(), a timer driven by
// Scheduler::ticksDue() in run(), and the BTP side (a btp::Node serving the
// catalog of buildPlantCatalog() over m_transport).
bool SimulatedPlant::load(const sim::ModelDefinition& model, const sim::SolverSettings& solver,
                          const sim::SampleClock& clock, QString* error) {
    Q_UNUSED(solver);
    Q_UNUSED(clock);
    m_model = model;
    if (error) {
        error->clear();
    }
    return false;
}

const sim::ModelDefinition& SimulatedPlant::model() const {
    return m_model;
}

SimulatedPlant::RunState SimulatedPlant::runState() const {
    return m_runState;
}

void SimulatedPlant::setTransport(traceview::Transport* transport) {
    m_transport = transport;
}

void SimulatedPlant::run() {}

void SimulatedPlant::pause() {}

void SimulatedPlant::reset() {}

void SimulatedPlant::step() {}

}  // namespace tracelab
