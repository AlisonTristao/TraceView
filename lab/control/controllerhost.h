#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <btp/node.hpp>
#include <memory>

#include "lab/control/controlleradapter.h"
#include "lab/control/signalfilter.h"

class QTimer;

namespace traceview {
class Transport;
}

namespace tracelab {

class BtpProducerPort;

// The controller slot, itself a BTP device (plan 5.1): "TraceLab:
// Controller" on the Devices tab. Two links:
//   - to the plant (setPlantTransport()): a BTP consumer -- HELLO, the
//     plant's manifest, SUBSCRIBE to loop.state and sim.preview, loop.mode
//     REMOTE + arm, then one loop.u STREAM request (Node::send_stream()) per
//     loop.state. Everything is bound by name from the plant's manifest.
//   - to TraceView (setViewerTransport()): a producer of ctl.diag and
//     ctl.info, like any device.
// It has no timer of its own: it runs the adapter on every loop.state that
// arrives.
//
// Contract, per loop.state of tick k:
//   1. The measurements become y[] in loop.state's declared field order
//      (tick and mode excluded; an array field contributes every element).
//      The filters (setFilters()), in order, turn them into the signals the
//      adapter is given -- by name: each one is told the names coming in and
//      answers the names going out (lab/control/signalfilter.h).
//   2. If a sim.preview of the same tick k was received (the plant sends it
//      first, see lab/plant/plantmanifest.h), it becomes
//      ControllerInput::reference (px[] then py[]). A preview of another
//      tick is never used: without one, the reference is empty.
//   3. ControllerAdapter::compute() runs once, timed.
//   4. The answer goes out as one loop.u STREAM packet: ref_tick = k and the
//      whole horizon u[0..M), one array per loop.u input.
//   5. ctl.diag is published for tick k: ref_tick, solve_ms (the measured
//      compute time), iterations, pred_x[], pred_y[], pred_theta[] (car
//      frame of tick k), one field per ControllerAdapter::diagNames()
//      entry, then each filter's SignalFilter::diagNames().
// ctl.info, a UTF8 document topic, carries ControllerAdapter::name(): sent
// on subscribe and whenever the adapter changes (the scene's info line).
// A tick lower than the last one is a sim.reset: the adapter is reset first.
class ControllerHost : public QObject {
    Q_OBJECT

public:
    // `sourceId`: this endpoint's BTP identity (non-zero), on both links.
    explicit ControllerHost(quint32 sourceId = 0x7C000001u, QObject* parent = nullptr);
    ~ControllerHost() override;

    // Takes ownership; the previous adapter is destroyed. ctl.diag's fields
    // follow the first adapter's diagNames(); later ones should keep them.
    void setAdapter(std::unique_ptr<ControllerAdapter> adapter);
    ControllerAdapter* adapter() const;
    // The stages between loop.state and the adapter, in order; replaces the
    // previous ones (destroyed).
    void setFilters(std::vector<std::unique_ptr<SignalFilter>> filters);
    const std::vector<std::unique_ptr<SignalFilter>>& filters() const {
        return m_filters;
    }
    // Hands the filters back (the host keeps none); setFilters() again to
    // put them, or others, back.
    std::vector<std::unique_ptr<SignalFilter>> takeFilters() {
        std::vector<std::unique_ptr<SignalFilter>> filters = std::move(m_filters);
        m_filters.clear();
        return filters;
    }
    // The plant's sample period, handed to the filters (default 1/80 s).
    void setSampleTime(double seconds) {
        m_sampleTime = seconds;
    }
    double sampleTime() const {
        return m_sampleTime;
    }
    // Re-sends ctl.info (after the adapter renamed itself).
    void refreshInfo();
    // Binds the filters and the adapter to the plant's signals again (after
    // their code changed what they read or write). Also re-sends ctl.info.
    void reconfigure();

    // The plant's measurement names (loop.state, arrays as "s[i]"), its
    // input names (loop.u), and the signals the adapter is given after the
    // filters. Empty before the plant's manifest is read.
    const QStringList& measurementNames() const {
        return m_measurementNames;
    }
    const QStringList& inputNames() const {
        return m_inputNames;
    }
    const QStringList& signalNames() const {
        return m_signalNames;
    }
    // The values of the last loop.state: as measured (measurementNames()
    // order) and as the adapter got them (signalNames() order).
    const QVector<double>& lastMeasurements() const {
        return m_lastMeasurements;
    }
    const QVector<double>& lastSignals() const {
        return m_lastSignals;
    }
    // Why the loop is not closed although the plant is ready: the filter or
    // controller that refused the plant's signals, and its reason. Empty when
    // configured.
    const QString& configureError() const {
        return m_configureError;
    }
    bool configured() const {
        return m_configured;
    }
    // The names ctl.diag publishes after its fixed fields: the adapter's
    // diagNames(), then the filters'.
    QStringList diagNames() const;
    QStringList diagUnits() const;

    // The link to the plant (normally a LoopbackTransport end facing the
    // plant's controller port). Not owned. Same as setPlantTransport().
    void setTransport(traceview::Transport* transport);
    void setPlantTransport(traceview::Transport* transport);
    // The link to TraceView. Not owned.
    void setViewerTransport(traceview::Transport* transport);

    // Bound to the plant: manifest read, subscriptions asked, REMOTE asked.
    bool plantReady() const {
        return m_ready;
    }
    quint64 lastTick() const {
        return m_lastTick;
    }
    // Ticks answered since the last reset of the counter.
    quint64 stepsAnswered() const {
        return m_answered;
    }

signals:
    void plantReadyChanged(bool ready);
    // configured()/configureError()/signalNames() changed.
    void configurationChanged();
    // ctl.diag's fields changed while TraceView held a session: they reach it
    // on its next session (the device may restart the link for that).
    void diagSchemaChanged();
    // After every answered tick.
    void stepped(quint64 tick, double solveMs);

private:
    using ConsumerNode =
        btp::StaticNode</*Slots=*/4, /*SlotBytes=*/16384, /*SealBytes=*/64,
                        /*ScratchBytes=*/2048, /*CatalogTopics=*/1, /*CatalogFields=*/1,
                        /*CatalogStringBytes=*/16, /*MaxSubscriptions=*/4,
                        /*MaxCommands=*/4, /*CommandBytes=*/64>;
    using LearnedCatalog = btp::StaticCatalog<16, 192, 8192, 8, 16>;
    using DiagCatalog = btp::StaticCatalog<4, 128, 4096, 0, 0>;
    class Link;

    void onPlantBytes(const QByteArray& data);
    void onPlantState(bool connected);
    void connectToPlant();
    void onCatalog();
    void onSample(const btp::CatalogTopic& topic, btp::SampleReader& reader);
    void answer(quint64 tick, const QVector<double>& y);
    void bind();
    void buildDiagCatalog();
    void sendInfo();
    static void sampleThunk(void* ctx, const btp::CatalogTopic& topic, btp::SampleReader& reader);

    quint32 m_sourceId;
    std::unique_ptr<ControllerAdapter> m_adapter;
    std::vector<std::unique_ptr<SignalFilter>> m_filters;

    // The plant side.
    std::unique_ptr<Link> m_link;
    std::unique_ptr<ConsumerNode> m_node;
    LearnedCatalog m_learned;
    QPointer<traceview::Transport> m_plantTransport;
    QTimer* m_tickTimer = nullptr;
    QTimer* m_retryTimer = nullptr;
    bool m_ready = false;
    quint32 m_peerSource = 0;
    quint32 m_peerBoot = 0;
    quint16 m_stateTopic = 0;
    quint16 m_previewTopic = 0;
    quint16 m_uAction = 0;
    quint16 m_modeAction = 0;
    QStringList m_measurementNames;
    QStringList m_inputNames;
    QStringList m_signalNames;
    QString m_configureError;
    QVector<double> m_lastMeasurements;
    QVector<double> m_lastSignals;
    bool m_configured = false;
    double m_sampleTime = 1.0 / 80.0;

    // The newest sim.preview.
    qint64 m_previewTick = -1;
    QVector<double> m_previewX;
    QVector<double> m_previewY;
    quint64 m_lastTick = 0;
    quint64 m_answered = 0;
    QByteArray m_scratch;

    // The TraceView side.
    DiagCatalog m_diagCatalog;
    QByteArray m_infoName;
    BtpProducerPort* m_viewer = nullptr;
    QPointer<traceview::Transport> m_viewerTransport;
    quint64 m_lastTimestampUs = 0;
};

}  // namespace tracelab
