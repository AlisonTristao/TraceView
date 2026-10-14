#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>

#include "telemetry/telemetrybinding.h"

namespace traceview {

// One topic a TelemetryConsumer wants delivered: the project Device it comes
// from ("deviceId", as in every widget config), where it currently lives
// (source and topic id, kept current by the binding resolver) and the rate
// to ask for. A consumer only lists bound topics: none of the fields is
// empty or zero.
struct TelemetryTopicRequest {
    QString deviceId;
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint32 rateMillihz = 0;
};

// A dashboard widget that reads telemetry from topics it picks itself --
// several of them, possibly on different devices -- rather than the single
// (deviceId, sourceId, topicId) a chart, gauge or text board has.
// MainWindow knows the built-in widget kinds by type, which a widget from an
// optional module (docs/MODULES.md) cannot be, so such a widget implements
// this next to DashboardWidget instead: MainWindow subscribes every topic in
// topicRequests() on that device's Backend (re-reading it after every config
// edit, undo/redo and binding re-resolution) and forwards the Backend's
// samples. The samples are not filtered: like the built-in widgets, a
// consumer keeps the ones whose (source, topic, field) it is bound to.
// Workspaces that are not showing keep no subscription for a consumer.
class TelemetryConsumer {
public:
    virtual QVector<TelemetryTopicRequest> topicRequests() const = 0;

    // Backend::fieldSample / arraySample / textSample, unchanged.
    virtual void onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                               double value) = 0;
    virtual void onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                               const QVector<float>& values) = 0;
    virtual void onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                              const QString& text) = 0;

    // How the consumer sends a command line to one of its devices (by
    // project Device id): MainWindow hands one over when it wires the widget,
    // delivered like a device script's sendCommand() (Backend::sendCommand(),
    // a BTP COMMAND_REQUEST). Optional: the default ignores it.
    using CommandSender =
        std::function<void(const QString& deviceId, const QString& commandLine)>;
    virtual void setCommandSender(CommandSender sender) {
        (void)sender;
    }

protected:
    ~TelemetryConsumer() = default;
};

}  // namespace traceview
