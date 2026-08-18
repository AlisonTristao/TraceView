#include "devicecard.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "dashboard/roundedcorners.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
// Same header scale as DashboardCell (see kHeaderHeight/kIconSize/
// kIconMargin/kStatusDotSize in dashboard/dashboardcell.cpp) -- kept as a
// separate copy here (DeviceCard doesn't depend on the dashboard library)
// rather than shared constants, but deliberately the same numbers so the two
// card kinds read as one visual family.
constexpr int kHeaderHeight = 24;
constexpr int kIconSize = 14;
constexpr int kIconMargin = 6;
constexpr int kStatusDotSize = 8;
constexpr int kBodyMargin = 10;
constexpr int kBodyLineHeight = 18;

// Chip/plug glyph for CommType::Btp -- a rounded body with pin legs on each
// side, same "draw it, don't fake it" live-QPainter approach as
// dashboard/dashboardcell.cpp's drawTypeIcon(). Switches on CommType (rather
// than a string id) so adding a new protocol without a case here is a
// compile warning, not a silent blank icon.
void drawCommTypeIcon(QPainter& painter, const QRect& r, CommType type, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.topLeft());
    const qreal s = r.width();

    switch (type) {
        case CommType::Btp: {
            QPen pinPen(color, 1.3);
            pinPen.setCapStyle(Qt::FlatCap);
            painter.setPen(pinPen);
            constexpr int kPins = 3;
            for (int i = 0; i < kPins; ++i) {
                const qreal y = s * (0.2 + i * 0.3);
                painter.drawLine(QPointF(0, y), QPointF(s * 0.14, y));
                painter.drawLine(QPointF(s * 0.86, y), QPointF(s, y));
            }
            painter.setPen(QPen(color, 1.3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(s * 0.14, s * 0.1, s * 0.72, s * 0.8), s * 0.12, s * 0.12);
            break;
        }
    }
    painter.restore();
}

// Settings gear glyph -- deliberately a fresh copy of DashboardCell's
// drawGearIcon() rather than a shared function: that one lives in
// dashboardcell.cpp's anonymous namespace (private to that TU), and this
// widget isn't meant to depend on the dashboard library at all.
void drawGearIcon(QPainter& painter, const QRect& r, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.center());
    const qreal s = r.width();
    const qreal bodyRadius = s * 0.28;
    const qreal toothOuterRadius = s * 0.44;

    QPen toothPen(color, s * 0.12);
    toothPen.setCapStyle(Qt::FlatCap);
    painter.setPen(toothPen);
    constexpr int kTeeth = 6;
    for (int i = 0; i < kTeeth; ++i) {
        painter.save();
        painter.rotate(360.0 / kTeeth * i);
        painter.drawLine(QPointF(0, -bodyRadius), QPointF(0, -toothOuterRadius));
        painter.restore();
    }

    painter.setPen(QPen(color, 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(0, 0), bodyRadius, bodyRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(0, 0), bodyRadius * 0.32, bodyRadius * 0.32);
    painter.restore();
}
} // namespace

DeviceCard::DeviceCard(QWidget* parent) : QWidget(parent) {
    setFixedSize(kDeviceCardSize);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

void DeviceCard::setDevice(const Device& device) {
    m_device = device;
    update();
}

void DeviceCard::setSelected(bool selected) {
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    update();
}

QRect DeviceCard::headerRect() const {
    return QRect(0, 0, width(), kHeaderHeight);
}

QRect DeviceCard::gearButtonRect() const {
    const QRect header = headerRect();
    const int y = (header.height() - kIconSize) / 2;
    return QRect(header.right() - kIconMargin - kIconSize + 1, y, kIconSize, kIconSize);
}

QRect DeviceCard::statusDotRect() const {
    // Same placement math paintEvent() uses to lay out the header row: comm-
    // type icon first, then the dot immediately after it.
    const QRect iconRect(kIconMargin, (kHeaderHeight - kIconSize) / 2, kIconSize, kIconSize);
    const int left = iconRect.right() + kIconMargin;
    return QRect(left, (kHeaderHeight - kStatusDotSize) / 2, kStatusDotSize, kStatusDotSize);
}

void DeviceCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    const QPainterPath outline =
        partiallyRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kContainerCornerRadius, true, true,
                              true, true);
    painter.fillPath(outline, palette.surface);
    painter.setPen(QPen(palette.border, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);

    painter.save();
    painter.setClipPath(outline);
    painter.fillRect(headerRect(), palette.surfaceAlt);
    painter.restore();

    const QColor headerFg = palette.textPrimary;
    QRect textRect = headerRect().adjusted(kIconMargin, 0, -kIconMargin, 0);

    const QRect iconRect(kIconMargin, (kHeaderHeight - kIconSize) / 2, kIconSize, kIconSize);
    drawCommTypeIcon(painter, iconRect, m_device.commType, headerFg);
    textRect.setLeft(iconRect.right() + kIconMargin);

    // Connection dot -- green/red, same palette.success/palette.danger
    // convention as DashboardCell's header dot. Also the click target for
    // connectToggleRequested() (see mousePressEvent()).
    const QRect dotRect = statusDotRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_device.connected ? palette.success : palette.danger);
    painter.drawEllipse(dotRect);
    textRect.setLeft(dotRect.right() + kIconMargin);

    textRect.setRight(gearButtonRect().left() - kIconMargin);
    drawGearIcon(painter, gearButtonRect(), headerFg);

    painter.setPen(headerFg);
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    const QFontMetrics titleMetrics(titleFont);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                      titleMetrics.elidedText(m_device.name, Qt::ElideRight, textRect.width()));

    // Body: comm-type label, then a one-line elided description below it.
    const QRect body = rect().adjusted(kBodyMargin, kHeaderHeight + kBodyMargin, -kBodyMargin, -kBodyMargin);
    QFont bodyFont = painter.font();
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    const QFontMetrics bodyMetrics(bodyFont);

    painter.setPen(palette.textSecondary);
    const QRect commTypeRect(body.left(), body.top(), body.width(), kBodyLineHeight);
    painter.drawText(commTypeRect, Qt::AlignVCenter | Qt::AlignLeft, commTypeLabel(m_device.commType));

    painter.setPen(palette.textPrimary);
    const QRect descRect(body.left(), commTypeRect.bottom() + 4, body.width(), body.height() - kBodyLineHeight - 4);
    painter.drawText(descRect, Qt::AlignTop | Qt::AlignLeft,
                      bodyMetrics.elidedText(m_device.description, Qt::ElideRight, descRect.width()));

    // Selection border -- same palette.accent convention as DashboardCell's
    // selected state, minus the color-blend animation (this card's selection
    // never competes with a drag-invalid state the way a dashboard cell's
    // does, so a flat border is enough). Drawn last, on top of everything
    // else: drawing it earlier (right after the base outline) left it
    // partially overpainted by the header's fillRect() in the top strip,
    // showing up as a notch where the border looked thinner across the
    // header than below it.
    if (m_selected) {
        painter.setPen(QPen(palette.accent, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }
}

void DeviceCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (gearButtonRect().contains(event->position().toPoint())) {
        emit configRequested(m_device.id);
        event->accept();
        return;
    }
    // Inflated a few px past the dot's tiny 8x8 paint size -- an 8px target
    // is otherwise unreasonably fussy to hit deliberately.
    if (statusDotRect().adjusted(-4, -4, 4, 4).contains(event->position().toPoint())) {
        emit connectToggleRequested(m_device.id);
        event->accept();
        return;
    }
    emit selectRequested(m_device.id);
    event->accept();
}

} // namespace traceview
