#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QHash>
#include <QJsonObject>
#include <QMultiHash>
#include <QVector>
#include <memory>

#include "dashboard/dashboardwidget.h"
#include "dashboard/telemetryconsumer.h"
#include "dashboard/scenes/scenerenderer.h"

namespace traceview {

class WidgetRegistry;

// The dashboard widget type id of the Scene widget.
inline constexpr char kSceneWidgetTypeId[] = "scene";

// What a Scene widget's config holds -- and all a .tvproj stores for it: the
// scene type, the bindings and the parameters. Never code, so a shared
// project is safe to open.
//
//   { "scene": "diffdrive",
//     "bind": { "x": { "deviceId": "...", "sourceId": "0x1A2B3C4D",
//                      "topicId": "0x0003", "topicName": "sim.truth",
//                      "fieldId": 2, "fieldName": "x" },
//               "track": { "deviceId": "...", "sourceId": "0x1A2B3C4D",
//                          "topicId": "0x0009", "topicName": "sim.track" } },
//     "params": { "scale_px_per_m": 300 } }
//
// Each "bind" entry is a binding of its own, in the same keys as a chart's
// (docs/DASHBOARD.md "Telemetry bindings"): the names are the binding and
// MainWindow re-points the ids from them, entry by entry. A Text input has no
// field keys. Inputs left unbound have no entry.
struct SceneBinding {
    QString deviceId;
    quint32 sourceId = 0;
    quint16 topicId = 0;
    QString topicName;
    int fieldId = -1;  // -1: a whole-topic (Text) binding
    QString fieldName;
};
SceneBinding parseSceneBinding(const QJsonObject& json);
QJsonObject sceneBindingToJson(const SceneBinding& binding);

// The Scene widget: hosts one SceneRenderer, feeds it the telemetry its
// inputs are bound to and paints it at the dashboard's render rate (Settings
// > Dashboard, the same coalesced repaint the charts use). Mouse wheel zooms,
// dragging pans, a double click resets the view (run mode only); keys go to
// the scene while it has focus.
class SceneWidget : public DashboardWidget, public TelemetryConsumer {
    Q_OBJECT

public:
    // Asked of every topic: 60 Hz, the dashboard's default frame rate. A
    // scene draws the latest pose; asking for more only loads the link (and
    // every other widget reading the same topic) with samples never drawn.
    static constexpr quint32 kRequestedRateMillihz = 60'000;
    // Records kept while the widget is not painted (a hidden tab).
    static constexpr int kMaxPendingRecords = 1024;

    explicit SceneWidget(QWidget* parent = nullptr);
    ~SceneWidget() override;

    void setConfig(const QJsonObject& config) override;
    void setEditModeHint(bool editMode) override;
    void paintHeaderIcon(QPainter& painter, const QRectF& rect,
                         const QColor& color) const override;

    QVector<traceview::TelemetryTopicRequest> topicRequests() const override;
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value) override;
    void onArraySample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       const QVector<float>& values) override;
    void onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                      const QString& text) override;
    // P/O/R and the other scene keys are sent through it (MainWindow hands
    // it over): "<action> <argument>" to the bound plant.
    void setCommandSender(CommandSender sender) override;
    // The project Device a key's action goes to: the one "x" is bound on.
    QString actionDeviceId() const;

    QString sceneType() const;
    // nullptr when the configured type is not registered.
    SceneRenderer* renderer() const;
    SceneView view() const;

signals:
    // A key asked the bound plant for `action` (sim.disturb...). Also sent,
    // as the text command "<action> <argument>", through the command sender.
    void actionRequested(const QString& action, double argument);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    SceneRecord& recordFor(quint32 sourceId, quint16 topicId, quint64 timestampUs);
    void sendAction(const QString& action, double argument);
    void scheduleRepaint();
    // The frame to hand the renderer: the records since the last one and the
    // latest values, then cleared.
    SceneFrame takeFrame();
    // Visible widgets render on a worker thread (startRender()); paintEvent()
    // only draws the finished image. Hidden ones (grab(), tests) paint inline.
    void startRender();
    void renderFinished(const QImage& image, quint64 generation);
    void noteFrame();

    QString m_sceneType;
    // Shared with the render job in flight; every use of the renderer, on
    // either thread, holds m_rendererMutex.
    std::shared_ptr<SceneRenderer> m_renderer;
    std::shared_ptr<QMutex> m_rendererMutex = std::make_shared<QMutex>();
    QImage m_image;              // the last finished frame
    bool m_rendering = false;    // a job in flight
    bool m_renderAgain = false;  // something changed while it ran
    quint64 m_generation = 0;    // bumped when the renderer is replaced
    QHash<QString, SceneBinding> m_bindings;
    // Bound inputs by where their data comes from: (source, topic, field) for
    // Scalar and Array inputs, (source, topic) for Text ones.
    QMultiHash<quint64, QString> m_scalarInputs;
    QMultiHash<quint64, QString> m_arrayInputs;
    QMultiHash<quint64, QString> m_textInputs;

    // Records since the last frame; the newest one is still open for the
    // other fields of its sample (same source, topic and timestamp).
    QVector<SceneRecord> m_pending;
    quint32 m_openSourceId = 0;
    quint16 m_openTopicId = 0;
    bool m_recordOpen = false;
    QHash<QString, double> m_values;
    QHash<QString, QVector<double>> m_arrays;
    // The last document of each Text input, replayed to a new renderer.
    QHash<QString, QByteArray> m_documents;

    double m_zoom = 1.0;
    QPointF m_pan;
    QPoint m_dragFrom;
    bool m_dragging = false;
    bool m_editMode = false;

    CommandSender m_commandSender;
    QElapsedTimer m_clock;
    QVector<qint64> m_frameTimesMs;
};

// Registers the "scene" widget type (and its config editor) on `registry`.
void registerSceneWidget(traceview::WidgetRegistry& registry);

}  // namespace traceview
