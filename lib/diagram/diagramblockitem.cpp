#include "diagramblockitem.h"

#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <utility>

#include "diagramportitem.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr qreal kWidth = 176.0;
constexpr qreal kHeaderHeight = 26.0;
constexpr qreal kPortRowHeight = 22.0;
constexpr qreal kPortCount = 3;
constexpr qreal kHeight = kHeaderHeight + kPortRowHeight * kPortCount + 6.0;
constexpr qreal kCornerRadius = 6.0;

constexpr DiagramPortKind kKinds[3] = {DiagramPortKind::Command, DiagramPortKind::Terminal,
                                       DiagramPortKind::Telemetry};
}  // namespace

DiagramBlockItem::DiagramBlockItem(QString deviceId, QString label, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_deviceId(std::move(deviceId)), m_label(std::move(label)) {
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(2);

    for (int i = 0; i < 3; ++i) {
        m_inputs[i] = new DiagramPortItem(kKinds[i], DiagramPortDirection::Input, this);
        m_outputs[i] = new DiagramPortItem(kKinds[i], DiagramPortDirection::Output, this);
    }
    layoutPorts();
}

void DiagramBlockItem::setLabel(const QString& label) {
    if (m_label == label) {
        return;
    }
    m_label = label;
    update();
}

void DiagramBlockItem::setLinkState(DeviceLinkState state) {
    if (m_linkState == state) {
        return;
    }
    m_linkState = state;
    update();
}

QRectF DiagramBlockItem::boundingRect() const {
    return QRectF(0, 0, kWidth, kHeight);
}

void DiagramBlockItem::layoutPorts() {
    for (int i = 0; i < 3; ++i) {
        const qreal y = kHeaderHeight + kPortRowHeight * i + kPortRowHeight / 2.0;
        m_inputs[i]->setPos(0, y);
        m_outputs[i]->setPos(kWidth, y);
    }
}

DiagramPortItem* DiagramBlockItem::port(DiagramPortKind kind, DiagramPortDirection direction) const {
    const DiagramPortItem* const* ports = direction == DiagramPortDirection::Input ? m_inputs
                                                                                    : m_outputs;
    for (int i = 0; i < 3; ++i) {
        if (kKinds[i] == kind) {
            return const_cast<DiagramPortItem*>(ports[i]);
        }
    }
    return nullptr;
}

void DiagramBlockItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF body = boundingRect();
    QPainterPath path;
    path.addRoundedRect(body, kCornerRadius, kCornerRadius);

    painter->setPen(QPen(isSelected() ? palette.accent : palette.border, isSelected() ? 2 : 1));
    painter->setBrush(palette.surface);
    painter->drawPath(path);

    // Header strip: label + link-state dot, same red/amber/green convention
    // as DeviceCard's status dot (devicecard.cpp). Clipped to the body's own
    // rounded-rect path so only its top two corners round, matching the
    // body outline instead of drawing a second independent shape.
    const QRectF header(0, 0, kWidth, kHeaderHeight);
    painter->save();
    painter->setClipPath(path);
    painter->setPen(Qt::NoPen);
    painter->setBrush(palette.surfaceAlt);
    painter->drawRect(header);
    painter->restore();

    const QColor dotColor = (m_linkState == DeviceLinkState::Live)      ? palette.success
                            : (m_linkState == DeviceLinkState::Offline) ? palette.danger
                                                                        : palette.warning;
    constexpr qreal kDotDiameter = 9.0;
    const QRectF dotRect(8, (kHeaderHeight - kDotDiameter) / 2.0, kDotDiameter, kDotDiameter);
    painter->setBrush(dotColor);
    painter->drawEllipse(dotRect);

    painter->setPen(palette.textPrimary);
    const QRectF labelRect(dotRect.right() + 6, 0, kWidth - dotRect.right() - 12, kHeaderHeight);
    const QFontMetrics metrics(painter->font());
    painter->drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(m_label, Qt::ElideRight, int(labelRect.width())));

    // Port row labels, centered between the two ports of each kind.
    painter->setPen(palette.textSecondary);
    for (int i = 0; i < 3; ++i) {
        const qreal y = kHeaderHeight + kPortRowHeight * i;
        const QRectF rowRect(10, y, kWidth - 20, kPortRowHeight);
        painter->drawText(rowRect, Qt::AlignCenter, diagramPortKindLabel(kKinds[i]));
    }
}

QVariant DiagramBlockItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == QGraphicsItem::ItemPositionHasChanged) {
        emit moved();
    }
    return QGraphicsObject::itemChange(change, value);
}

void DiagramBlockItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    emit activated(m_deviceId);
    QGraphicsObject::mouseDoubleClickEvent(event);
}

}  // namespace traceview
