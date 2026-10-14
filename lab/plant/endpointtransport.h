#pragma once

#include <QPointer>
#include <functional>

#include "core/moduledevicetype.h"
#include "lab/plant/loopbacktransport.h"

namespace tracelab {

// The Transport TraceView's DeviceConnection gets for a TraceLab device
// (a ModuleDeviceType's factory returns one): the TraceView end of a fresh
// LoopbackTransport pair per open(), the other end handed over by `attach`,
// which plugs it into the in-process endpoint (a BtpProducerPort). So the
// device's BTP crosses a real (if in-memory) link, with the same queued,
// never-reentrant delivery as a cable.
class EndpointTransport : public traceview::ModuleTransport {
    Q_OBJECT

public:
    // Plugs `peer` (already connected to this transport's own end) into the
    // endpoint. Returns false, with `error`, when the endpoint cannot come up
    // now (DeviceConnection retries).
    using Attach = std::function<bool(LoopbackTransport* peer, QString* error)>;

    explicit EndpointTransport(Attach attach, QObject* parent = nullptr);
    ~EndpointTransport() override;

    void open() override;
    void close() override;
    bool isConnected() const override;
    bool write(const QByteArray& data) override;

private:
    void dropPair();

    Attach m_attach;
    QPointer<LoopbackTransport> m_own;
    QPointer<LoopbackTransport> m_peer;
};

}  // namespace tracelab
