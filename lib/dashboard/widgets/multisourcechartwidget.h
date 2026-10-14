#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "dashboard/telemetryconsumer.h"
#include "dashboard/widgets/chartwidgets.h"

namespace traceview {

// A line chart whose series come from different topics, on different
// devices if need be -- a simulated plant's true value next to a filter's
// estimate published by its controller, say. Everything a line chart draws
// is the same (it is one); only where the samples come from differs: each
// series has a binding of its own under "bind" (keys seriesKey(i): "s0",
// "s1"...), with its own "deviceId", topic and field -- the shape the
// binding resolver re-points by name (bindingresolver.h, MainWindow::
// resolveWidgetBinding()) -- and the widget is a TelemetryConsumer, so
// MainWindow subscribes every topic it lists. Its config is a line chart's
// otherwise ("series" for the names, colors and styles, "xAxis", "yAxis",
// "view"); a top-level device/topic is ignored. No history is replayed into
// it when it is built: it plots what arrives from then on.
class MultiSourceLineChartWidget : public DummyLineChartWidget, public TelemetryConsumer {
public:
    // Where one series reads from.
    struct Binding {
        QString deviceId;
        quint32 sourceId = 0;
        quint16 topicId = 0;
        quint16 fieldId = 0;
        QString topicName;
        QString fieldName;

        bool isBound() const {
            return !deviceId.isEmpty() && sourceId != 0 && topicId != 0;
        }
        static Binding fromJson(const QJsonObject& json);
        QJsonObject toJson() const;
    };

    explicit MultiSourceLineChartWidget(QWidget* parent = nullptr);

    static QString seriesKey(int series);
    // The bindings of `config`'s series, in series order (an unbound one
    // where "bind" has no entry for it).
    static QVector<Binding> bindingsOf(const QJsonObject& config);

    void setConfig(const QJsonObject& config) override;
    const QVector<Binding>& bindings() const {
        return m_bindings;
    }

    QVector<TelemetryTopicRequest> topicRequests() const override;
    void onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value) override;
    void onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                       const QVector<float>& values) override;
    void onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                      const QString& text) override;

private:
    QVector<Binding> m_bindings;
    quint32 m_rateMillihz = 0;
    // (source, topic, field) -> the series plotting it, by the field id
    // the line chart underneath knows each by (its position + 1).
    QHash<quint64, QVector<quint16>> m_routes;
};

}  // namespace traceview
