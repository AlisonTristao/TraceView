#include "scenewidget.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>

#include "dashboard/paintframecounter.h"
#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgetregistry.h"
#include "lab/scenes/sceneconfigeditor.h"
#include "lab/scenes/sceneregistry.h"
#include "preferences/appsettings.h"

namespace tracelab {

namespace {

quint64 fieldKey(quint32 sourceId, quint16 topicId, quint16 fieldId) {
    return (quint64(sourceId) << 32) | (quint64(topicId) << 16) | fieldId;
}

quint64 topicKey(quint32 sourceId, quint16 topicId) {
    return (quint64(sourceId) << 16) | topicId;
}

bool sameBinding(const SceneBinding& a, const SceneBinding& b) {
    return a.deviceId == b.deviceId && a.sourceId == b.sourceId && a.topicId == b.topicId &&
           a.fieldId == b.fieldId;
}

constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 20.0;

}  // namespace

SceneBinding parseSceneBinding(const QJsonObject& json) {
    SceneBinding binding;
    binding.deviceId = json.value(QLatin1String("deviceId")).toString();
    binding.sourceId =
        quint32(json.value(QLatin1String("sourceId")).toString("0").toULongLong(nullptr, 0));
    binding.topicId = quint16(
        qBound(0, json.value(QLatin1String("topicId")).toString("0").toInt(nullptr, 0), 65535));
    binding.topicName = json.value(QLatin1String("topicName")).toString();
    if (json.contains(QLatin1String("fieldId"))) {
        binding.fieldId = qBound(0, json.value(QLatin1String("fieldId")).toInt(0), 65535);
    }
    binding.fieldName = json.value(QLatin1String("fieldName")).toString();
    return binding;
}

QJsonObject sceneBindingToJson(const SceneBinding& binding) {
    QJsonObject json;
    json[QLatin1String("deviceId")] = binding.deviceId;
    json[QLatin1String("sourceId")] = traceview::formatHexId(binding.sourceId, 8);
    json[QLatin1String("topicId")] = traceview::formatHexId(binding.topicId, 4);
    if (!binding.topicName.isEmpty()) {
        json[QLatin1String("topicName")] = binding.topicName;
    }
    if (binding.fieldId >= 0) {
        json[QLatin1String("fieldId")] = binding.fieldId;
        if (!binding.fieldName.isEmpty()) {
            json[QLatin1String("fieldName")] = binding.fieldName;
        }
    }
    return json;
}

SceneWidget::SceneWidget(QWidget* parent) : DashboardWidget(parent) {
    setFocusPolicy(Qt::ClickFocus);
    m_clock.start();
    m_repaintIntervalMs = traceview::AppSettings::instance().repaintIntervalMs();
    connect(&traceview::AppSettings::instance(),
            &traceview::AppSettings::dashboardPreferencesChanged, this, [this] {
                m_repaintIntervalMs = traceview::AppSettings::instance().repaintIntervalMs();
            });
    setConfig({});
}

SceneWidget::~SceneWidget() = default;

void SceneWidget::setConfig(const QJsonObject& config) {
    // No type yet (a widget just added): the first registered scene.
    QString type = config.value(QLatin1String("scene")).toString();
    const QVector<SceneTypeInfo> types = SceneRegistry::instance().types();
    if (type.isEmpty() && !types.isEmpty()) {
        type = types.first().typeId;
    }
    if (type != m_sceneType || !m_renderer) {
        m_sceneType = type;
        m_renderer = SceneRegistry::instance().create(type);
        m_pending.clear();
        m_recordOpen = false;
        m_values.clear();
        m_arrays.clear();
        m_documents.clear();
    }

    QHash<QString, SceneBinding> bindings;
    const QJsonObject bind = config.value(QLatin1String("bind")).toObject();
    for (auto it = bind.constBegin(); it != bind.constEnd(); ++it) {
        bindings.insert(it.key(), parseSceneBinding(it.value().toObject()));
    }
    // An input bound elsewhere now drops what it showed.
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        if (!bindings.contains(it.key()) || !sameBinding(bindings.value(it.key()), it.value())) {
            m_values.remove(it.key());
            m_arrays.remove(it.key());
            m_documents.remove(it.key());
        }
    }
    m_bindings = bindings;

    m_scalarInputs.clear();
    m_arrayInputs.clear();
    m_textInputs.clear();
    if (m_renderer) {
        for (const SceneInput& input : m_renderer->inputs()) {
            if (!m_bindings.contains(input.name)) {
                continue;
            }
            const SceneBinding& binding = m_bindings[input.name];
            switch (input.kind) {
                case SceneInput::Kind::Scalar:
                    if (binding.fieldId >= 0) {
                        m_scalarInputs.insert(
                            fieldKey(binding.sourceId, binding.topicId, quint16(binding.fieldId)),
                            input.name);
                    }
                    break;
                case SceneInput::Kind::Array:
                    if (binding.fieldId >= 0) {
                        m_arrayInputs.insert(
                            fieldKey(binding.sourceId, binding.topicId, quint16(binding.fieldId)),
                            input.name);
                    }
                    break;
                case SceneInput::Kind::Text:
                    m_textInputs.insert(topicKey(binding.sourceId, binding.topicId), input.name);
                    break;
            }
        }

        const QJsonObject params = config.value(QLatin1String("params")).toObject();
        for (const SceneParam& param : m_renderer->params()) {
            m_renderer->setParam(param.name, params.value(param.name).toDouble(param.defaultValue));
        }
        for (auto it = m_documents.constBegin(); it != m_documents.constEnd(); ++it) {
            m_renderer->document(it.key(), it.value());
        }
    }
    update();
}

void SceneWidget::setEditModeHint(bool editMode) {
    m_editMode = editMode;
    m_dragging = false;
}

QVector<traceview::TelemetryTopicRequest> SceneWidget::topicRequests() const {
    QVector<traceview::TelemetryTopicRequest> requests;
    for (const SceneBinding& binding : m_bindings) {
        if (binding.deviceId.isEmpty() || binding.sourceId == 0 || binding.topicId == 0) {
            continue;
        }
        bool listed = false;
        for (const traceview::TelemetryTopicRequest& request : requests) {
            if (request.deviceId == binding.deviceId && request.sourceId == binding.sourceId &&
                request.topicId == binding.topicId) {
                listed = true;
                break;
            }
        }
        if (!listed) {
            requests.append(
                {binding.deviceId, binding.sourceId, binding.topicId, kRequestedRateMillihz});
        }
    }
    return requests;
}

SceneRecord& SceneWidget::recordFor(quint32 sourceId, quint16 topicId, quint64 timestampUs) {
    if (!m_recordOpen || m_openSourceId != sourceId || m_openTopicId != topicId ||
        m_pending.last().timestampUs != timestampUs) {
        if (m_pending.size() >= kMaxPendingRecords) {
            m_pending.remove(0, m_pending.size() / 2);
        }
        SceneRecord record;
        record.timestampUs = timestampUs;
        m_pending.append(record);
        m_openSourceId = sourceId;
        m_openTopicId = topicId;
        m_recordOpen = true;
    }
    return m_pending.last();
}

void SceneWidget::onFieldSample(const traceview::TelemetryFieldBinding& binding,
                                quint64 timestampUs, double value) {
    if (binding.elementIndex != 0) {
        return;
    }
    const QList<QString> inputs =
        m_scalarInputs.values(fieldKey(binding.sourceId, binding.topicId, binding.fieldId));
    if (inputs.isEmpty()) {
        return;
    }
    SceneRecord& record = recordFor(binding.sourceId, binding.topicId, timestampUs);
    for (const QString& input : inputs) {
        record.values.insert(input, value);
        m_values.insert(input, value);
    }
    scheduleRepaint();
}

void SceneWidget::onArraySample(const traceview::TelemetryFieldBinding& binding,
                                quint64 timestampUs, const QVector<float>& values) {
    const QList<QString> inputs =
        m_arrayInputs.values(fieldKey(binding.sourceId, binding.topicId, binding.fieldId));
    if (inputs.isEmpty()) {
        return;
    }
    QVector<double> numbers;
    numbers.reserve(values.size());
    for (const float value : values) {
        numbers.append(double(value));
    }
    SceneRecord& record = recordFor(binding.sourceId, binding.topicId, timestampUs);
    for (const QString& input : inputs) {
        record.arrays.insert(input, numbers);
        m_arrays.insert(input, numbers);
    }
    scheduleRepaint();
}

void SceneWidget::onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                               const QString& text) {
    Q_UNUSED(timestampUs);
    const QList<QString> inputs = m_textInputs.values(topicKey(sourceId, topicId));
    if (inputs.isEmpty()) {
        return;
    }
    const QByteArray body = text.toUtf8();
    for (const QString& input : inputs) {
        m_documents.insert(input, body);
        if (m_renderer) {
            m_renderer->document(input, body);
        }
    }
    scheduleRepaint();
}

QString SceneWidget::sceneType() const {
    return m_sceneType;
}

SceneRenderer* SceneWidget::renderer() const {
    return m_renderer.get();
}

SceneView SceneWidget::view() const {
    SceneView view;
    view.size = QSizeF(size());
    view.zoom = m_zoom;
    view.pan = m_pan;
    view.renderFps = m_frameTimesMs.size();
    return view;
}

void SceneWidget::scheduleRepaint() {
    if (m_repaintPending) {
        return;
    }
    m_repaintPending = true;
    QTimer::singleShot(m_repaintIntervalMs, this, [this] {
        m_repaintPending = false;
        update();
    });
}

void SceneWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    traceview::notePaintFrame();
    const qint64 now = m_clock.elapsed();
    m_frameTimesMs.append(now);
    while (!m_frameTimesMs.isEmpty() && m_frameTimesMs.first() <= now - 1000) {
        m_frameTimesMs.removeFirst();
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipPath(contentFillPath());
    if (!m_renderer) {
        painter.fillRect(rect(), Qt::white);
        painter.setPen(Qt::black);
        painter.drawText(rect(), Qt::AlignCenter, tr("Unknown scene \"%1\"").arg(m_sceneType));
        return;
    }
    SceneFrame frame;
    frame.records = m_pending;
    frame.values = m_values;
    frame.arrays = m_arrays;
    m_pending.clear();
    m_recordOpen = false;
    m_renderer->update(frame);
    m_renderer->paint(painter, view());
}

void SceneWidget::wheelEvent(QWheelEvent* event) {
    if (m_editMode) {
        event->ignore();
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    m_zoom = qBound(kMinZoom, m_zoom * std::pow(1.1, steps), kMaxZoom);
    event->accept();
    update();
}

void SceneWidget::mousePressEvent(QMouseEvent* event) {
    if (m_editMode || event->button() != Qt::LeftButton) {
        DashboardWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_dragFrom = event->position().toPoint();
    event->accept();
}

void SceneWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) {
        DashboardWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint at = event->position().toPoint();
    m_pan += QPointF(at - m_dragFrom);
    m_dragFrom = at;
    event->accept();
    update();
}

void SceneWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_dragging) {
        DashboardWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    event->accept();
}

void SceneWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_editMode) {
        DashboardWidget::mouseDoubleClickEvent(event);
        return;
    }
    m_zoom = 1.0;
    m_pan = QPointF();
    event->accept();
    update();
}

void SceneWidget::keyPressEvent(QKeyEvent* event) {
    if (m_renderer && !event->isAutoRepeat()) {
        if (const std::optional<SceneAction> action = m_renderer->keyAction(event->key())) {
            emit actionRequested(action->action, action->argument);
            event->accept();
            return;
        }
    }
    DashboardWidget::keyPressEvent(event);
}

void registerSceneWidget(traceview::WidgetRegistry& registry) {
    registry.registerType(
        {QString::fromLatin1(kSceneWidgetTypeId),
         QCoreApplication::translate("SceneWidget", "Scene"),
         [](QWidget* parent) -> traceview::DashboardWidget* { return new SceneWidget(parent); },
         [](QWidget* parent) -> traceview::WidgetConfigEditor* {
             return new SceneConfigEditor(parent);
         }});
}

}  // namespace tracelab
