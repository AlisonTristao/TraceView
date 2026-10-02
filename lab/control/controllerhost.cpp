#include "controllerhost.h"

#include "core/transport.h"

namespace tracelab {

// To write (plan phase T3): the BTP side (subscribe to loop.state and
// sim.preview on the plant, send loop.u, serve ctl.diag) and the per-tick
// contract of the header.
ControllerHost::ControllerHost(QObject* parent) : QObject(parent) {}

ControllerHost::~ControllerHost() = default;

void ControllerHost::setAdapter(std::unique_ptr<ControllerAdapter> adapter) {
    m_adapter = std::move(adapter);
}

ControllerAdapter* ControllerHost::adapter() const {
    return m_adapter.get();
}

void ControllerHost::setTransport(traceview::Transport* transport) {
    m_transport = transport;
}

}  // namespace tracelab
