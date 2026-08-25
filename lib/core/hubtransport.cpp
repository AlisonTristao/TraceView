#include "core/hubtransport.h"

#include "core/deviceconnection.h"
#include "protocol/btpbackend.h"

namespace traceview {

HubTransport::HubTransport(quint32 peerSourceId, QObject* parent)
    : Transport(parent), m_peerSourceId(peerSourceId) {}

void HubTransport::detach() {
    if (m_parent) {
        disconnect(m_parent, nullptr, this, nullptr);
    }
    if (m_parentBackend) {
        disconnect(m_parentBackend, nullptr, this, nullptr);
    }
    m_parent = nullptr;
    m_parentBackend = nullptr;
}

void HubTransport::attachTo(DeviceConnection* parent) {
    const bool wasConnected = isConnected();
    detach();

    m_parent = parent;
    // A hub channel is BTP frames demultiplexed by BTP source_id, so it can
    // only ride a BTP parent. A parent speaking some future protocol simply
    // yields no backend here and the child stays permanently disconnected,
    // rather than half-attaching and silently dropping everything.
    m_parentBackend = parent != nullptr ? qobject_cast<BtpBackend*>(parent->backend()) : nullptr;

    if (m_parentBackend) {
        connect(m_parentBackend, &BtpBackend::hubFrameBytesReceived, this,
                &HubTransport::onParentFrameBytes);
    }
    if (m_parent) {
        connect(m_parent, &DeviceConnection::connectionStateChanged, this,
                &HubTransport::onParentConnectionChanged);
    }

    m_open = isConnected();
    if (m_open != wasConnected) {
        emit connectionStateChanged(m_open);
    }
}

void HubTransport::setPeerSourceId(quint32 peerSourceId) {
    if (peerSourceId == m_peerSourceId) {
        return;
    }
    const bool wasConnected = isConnected();
    m_peerSourceId = peerSourceId;
    m_open = isConnected();
    if (m_open != wasConnected) {
        emit connectionStateChanged(m_open);
    }
}

bool HubTransport::isConnected() const {
    // Three conjuncts, and each is a distinct way for a child to be
    // unreachable: no hub to ride, a hub that is not on the wire, or a child
    // nobody has told which robot it is for.
    return m_parentBackend != nullptr && m_parent != nullptr && m_parent->isConnected() &&
           m_peerSourceId != 0;
}

void HubTransport::close() {
    // Detaching from the parent is what "closing" means here: there is no port
    // to release and nothing to tell the far end. The parent's own connection
    // is emphatically NOT closed -- it is shared with the hub device itself
    // and very likely with sibling children, so one child going away must not
    // take the cable down with it.
    const bool wasConnected = m_open;
    detach();
    m_open = false;
    if (wasConnected) {
        emit connectionStateChanged(false);
    }
}

bool HubTransport::write(const QByteArray& frame) {
    if (!isConnected() || frame.isEmpty()) {
        return false;
    }
    return m_parentBackend->sendChildFrame(frame);
}

void HubTransport::onParentFrameBytes(quint32 sourceId, const QByteArray& raw) {
    // The whole demux, in one comparison. Every child sees every frame the
    // parent decoded and keeps only its own; a frame addressed to nobody --
    // the hub's own telemetry, for instance -- is claimed by no child and is
    // consumed by the parent device itself, which is the correct outcome and
    // needs no rule of its own.
    if (sourceId != m_peerSourceId || m_peerSourceId == 0) {
        return;
    }
    emit dataReceived(raw);
}

void HubTransport::onParentConnectionChanged(bool) {
    // Recomputed rather than taken from the argument: the parent's state is
    // only one of this transport's three conditions (see isConnected()).
    const bool nowConnected = isConnected();
    if (nowConnected == m_open) {
        return;
    }
    m_open = nowConnected;
    emit connectionStateChanged(m_open);
}

}  // namespace traceview
