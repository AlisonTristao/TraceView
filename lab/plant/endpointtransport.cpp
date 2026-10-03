#include "endpointtransport.h"

namespace tracelab {

EndpointTransport::EndpointTransport(Attach attach, QObject* parent)
    : traceview::ModuleTransport(parent), m_attach(std::move(attach)) {}

EndpointTransport::~EndpointTransport() {
    dropPair();
}

void EndpointTransport::open() {
    if (isConnected()) {
        return;
    }
    dropPair();
    m_own = new LoopbackTransport(this);
    // The endpoint's end lives with the endpoint (parented there by attach),
    // or here until then.
    m_peer = new LoopbackTransport(this);
    connect(m_own, &traceview::Transport::dataReceived, this,
            &traceview::Transport::dataReceived);
    connect(m_own, &traceview::Transport::errorOccurred, this,
            &traceview::Transport::errorOccurred);
    QString error;
    // Paired first, so the endpoint sees its end already connected.
    m_own->connectTo(m_peer);
    if (!m_attach || !m_attach(m_peer, &error)) {
        dropPair();
        emit errorOccurred(error.isEmpty() ? tr("the built-in device did not start") : error);
        return;
    }
    connect(m_own, &traceview::Transport::connectionStateChanged, this,
            &traceview::Transport::connectionStateChanged);
    emit connectionStateChanged(true);
}

void EndpointTransport::close() {
    const bool was = isConnected();
    dropPair();
    if (was) {
        emit connectionStateChanged(false);
    }
}

bool EndpointTransport::isConnected() const {
    return m_own && m_own->isConnected();
}

bool EndpointTransport::write(const QByteArray& data) {
    return m_own && m_own->write(data);
}

void EndpointTransport::dropPair() {
    if (m_own) {
        disconnect(m_own, nullptr, this, nullptr);
        m_own->close();
        m_own->deleteLater();
    }
    if (m_peer) {
        m_peer->close();
        m_peer->deleteLater();
    }
    m_own = nullptr;
    m_peer = nullptr;
}

}  // namespace tracelab
