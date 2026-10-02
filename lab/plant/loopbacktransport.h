#pragma once

#include <QByteArray>
#include <QPointer>

#include "core/transport.h"

namespace tracelab {

// One end of an in-process byte pipe implementing traceview::Transport, so
// the simulated plant and the controller host speak real BTP to each other
// and to TraceView exactly as a robot does over a cable (plan decision L4).
// connectTo() pairs two ends: from then on what one end write()s arrives on
// the other's dataReceived(), in order, through a queued signal -- never
// inside the write() call, the same re-entrancy a real port gives.
// Simulated delay and loss (plan 4.2) belong here later.
class LoopbackTransport : public traceview::Transport {
    Q_OBJECT

public:
    explicit LoopbackTransport(QObject* parent = nullptr);
    ~LoopbackTransport() override;

    // Pairs this end with `peer` (both must be unpaired) and opens both:
    // each emits connectionStateChanged(true).
    void connectTo(LoopbackTransport* peer);

    // Closes both ends: each emits connectionStateChanged(false). Bytes
    // already queued are still delivered only if the receiving end is open.
    void close() override;
    bool isConnected() const override;
    bool write(const QByteArray& data) override;

signals:
    // Internal: carries written bytes to the peer, queued.
    void bytesForPeer(const QByteArray& data);

private:
    void deliver(const QByteArray& data);
    void closeOwnEnd();

    QPointer<LoopbackTransport> m_peer;
    bool m_open = false;
};

}  // namespace tracelab
