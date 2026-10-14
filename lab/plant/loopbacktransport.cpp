#include "loopbacktransport.h"

namespace tracelab {

LoopbackTransport::LoopbackTransport(QObject* parent) : traceview::Transport(parent) {}

LoopbackTransport::~LoopbackTransport() {
    if (m_peer) {
        m_peer->closeOwnEnd();
        m_peer->m_peer = nullptr;
    }
}

void LoopbackTransport::connectTo(LoopbackTransport* peer) {
    if (!peer || peer == this || m_peer || peer->m_peer) {
        return;
    }
    m_peer = peer;
    peer->m_peer = this;
    connect(this, &LoopbackTransport::bytesForPeer, peer, &LoopbackTransport::deliver,
            Qt::QueuedConnection);
    connect(peer, &LoopbackTransport::bytesForPeer, this, &LoopbackTransport::deliver,
            Qt::QueuedConnection);
    m_open = true;
    peer->m_open = true;
    emit connectionStateChanged(true);
    emit peer->connectionStateChanged(true);
}

void LoopbackTransport::close() {
    if (m_peer) {
        m_peer->closeOwnEnd();
    }
    closeOwnEnd();
}

bool LoopbackTransport::isConnected() const {
    return m_open;
}

bool LoopbackTransport::write(const QByteArray& data) {
    if (!m_open || !m_peer) {
        return false;
    }
    emit bytesForPeer(data);
    return true;
}

void LoopbackTransport::deliver(const QByteArray& data) {
    if (m_open) {
        emit dataReceived(data);
    }
}

void LoopbackTransport::closeOwnEnd() {
    if (!m_open) {
        return;
    }
    m_open = false;
    emit connectionStateChanged(false);
}

}  // namespace tracelab
