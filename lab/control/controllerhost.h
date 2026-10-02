#pragma once

#include <QObject>
#include <QVector>
#include <memory>

#include "lab/control/controlleradapter.h"

namespace traceview {
class Transport;
}

namespace tracelab {

// The controller slot, itself a BTP device (plan 5.1). It has no timer of
// its own: it runs the adapter on every loop.state that arrives.
//
// Contract, per loop.state of tick k:
//   1. The measurements become y[] in loop.state's declared field order
//      (the tick field excluded).
//   2. If a sim.preview of the same tick k was received (the plant sends it
//      first, see lab/plant/plantmanifest.h), it becomes
//      ControllerInput::reference. A preview of another tick is never used:
//      without one, the reference is empty.
//   3. ControllerAdapter::compute() runs once, timed.
//   4. The answer goes out as one loop.u STREAM packet: ref_tick = k and the
//      whole horizon u[0..M).
//   5. ctl.diag is published for tick k: ref_tick, solve_ms (the measured
//      compute time), iterations, pred_x[] and pred_y[] (car frame of tick
//      k -- the scene turns them into world coordinates with the pose of that
//      tick) and one field per ControllerAdapter::diagNames() entry.
// ctl.info, a UTF8 document topic, carries ControllerAdapter::name(): sent
// on connect and whenever the adapter changes (the scene's info line).
// A loop.state that arrives while compute() still runs for an older one is
// not queued: the newest replaces it (real time) -- in lockstep there is
// never more than one in flight.
class ControllerHost : public QObject {
    Q_OBJECT

public:
    explicit ControllerHost(QObject* parent = nullptr);
    ~ControllerHost() override;

    // Takes ownership; the previous adapter is destroyed.
    void setAdapter(std::unique_ptr<ControllerAdapter> adapter);
    ControllerAdapter* adapter() const;

    // Where the host's BTP goes (normally a LoopbackTransport end facing the
    // plant). Not owned.
    void setTransport(traceview::Transport* transport);

private:
    std::unique_ptr<ControllerAdapter> m_adapter;
    traceview::Transport* m_transport = nullptr;
};

}  // namespace tracelab
