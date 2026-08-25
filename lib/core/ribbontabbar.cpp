#include "ribbontabbar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "ribbon.h"
#include "traceview/theme.h"
#include "traceview/thememanager.h"

namespace traceview {

RibbonTabBar::RibbonTabBar(QWidget* parent) : QTabBar(parent) {
    setMouseTracking(true);
    setExpanding(false);  // keep every tab at tabSizeHint()'s fixed size, even
                          // when the bar has room to stretch them
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

QSize RibbonTabBar::tabSizeHint(int /*index*/) const {
    return QSize(kRibbonTabWidth, kRibbonTabHeight);
}

namespace {
// Small enough to sit comfortably inside the trapezoid's narrow top edge
// (see closeButtonRect()) without touching the slanted sides.
constexpr int kCloseButtonSize = 12;
constexpr int kCloseButtonMargin = 5;
}  // namespace

void RibbonTabBar::setTabClosable(int index, bool closable) {
    setTabData(index, closable);
}

QRect RibbonTabBar::closeButtonRect(int index) const {
    if (!tabData(index).toBool()) {
        return QRect();
    }
    const QRect r = tabRect(index).adjusted(0, 0, -2, 0);
    return QRect(r.right() - kRibbonTabSlant - kCloseButtonMargin - kCloseButtonSize,
                 r.top() + kCloseButtonMargin, kCloseButtonSize, kCloseButtonSize);
}

void RibbonTabBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    // Use the window-background token (not surface/surfaceAlt) so the strip
    // behind the trapezoids reads as a third, contrasting tone against both
    // the selected and unselected tab fills below.
    painter.fillRect(rect(), palette.background);

    for (int i = 0; i < count(); ++i) {
        // Shrink 2px off the right edge so adjacent trapezoids read as
        // separate file tabs rather than one fused strip.
        const QRectF r = QRectF(tabRect(i)).adjusted(0, 0, -2, 0);
        const bool selected = i == currentIndex();
        const bool enabled = isTabEnabled(i);

        QPainterPath path;
        path.moveTo(r.left() + kRibbonTabSlant, r.top());
        path.lineTo(r.right() - kRibbonTabSlant, r.top());
        path.lineTo(r.right(), r.bottom());
        path.lineTo(r.left(), r.bottom());
        path.closeSubpath();

        QColor fill = selected ? palette.surfaceAlt : palette.surface;
        if (!selected && enabled && i == m_hoverIndex) {
            fill = palette.surfaceAlt.lighter(112);
        }

        painter.setPen(QPen(palette.border, 1));
        painter.setBrush(fill);
        painter.drawPath(path);

        if (selected) {
            // Erase the base edge so the selected tab visually merges into
            // the active page painted directly below it (same fill color).
            painter.setPen(QPen(fill, 1));
            painter.drawLine(QPointF(r.left() + 1, r.bottom()), QPointF(r.right() - 1, r.bottom()));
        }

        const QRect closeRect = closeButtonRect(i);

        painter.setPen(enabled ? (selected ? palette.textPrimary : palette.textSecondary)
                               : palette.textDisabled);
        const int textMargin = kRibbonTabSlant + 4;
        // Closable tabs give up their right edge to the close button --
        // centering the title on the full trapezoid width (like a plain
        // tab) would run it straight underneath the "x" instead of leaving
        // that corner clear.
        QRectF textRect = r;
        if (!closeRect.isEmpty()) {
            textRect.setRight(closeRect.left() - 3);
        }
        const QString text =
            fontMetrics().elidedText(tabText(i), Qt::ElideRight,
                                     qMax(0, static_cast<int>(textRect.width()) - 2 * textMargin));
        painter.drawText(textRect, Qt::AlignCenter, text);

        if (!closeRect.isEmpty()) {
            const bool hoverClose = i == m_hoverCloseIndex;
            if (hoverClose) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette.danger.lighter(160));
                painter.drawRoundedRect(closeRect.adjusted(-2, -2, 2, 2), 3, 3);
            }
            QPen xPen(hoverClose ? palette.danger
                                 : (enabled ? palette.textSecondary : palette.textDisabled));
            xPen.setWidthF(1.4);
            painter.setPen(xPen);
            const QRectF xr = QRectF(closeRect).adjusted(2, 2, -2, -2);
            painter.drawLine(xr.topLeft(), xr.bottomRight());
            painter.drawLine(xr.topRight(), xr.bottomLeft());
        }
    }
}

void RibbonTabBar::mouseMoveEvent(QMouseEvent* event) {
    const int index = tabAt(event->pos());
    int hoverClose = -1;
    for (int i = 0; i < count(); ++i) {
        if (closeButtonRect(i).contains(event->pos())) {
            hoverClose = i;
            break;
        }
    }
    if (index != m_hoverIndex || hoverClose != m_hoverCloseIndex) {
        m_hoverIndex = index;
        m_hoverCloseIndex = hoverClose;
        update();
    }
    QTabBar::mouseMoveEvent(event);
}

void RibbonTabBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < count(); ++i) {
            if (closeButtonRect(i).contains(event->pos())) {
                emit tabCloseRequested(i);
                return;  // swallowed -- doesn't fall through to tab selection
            }
        }
    }
    QTabBar::mousePressEvent(event);
}

void RibbonTabBar::leaveEvent(QEvent* event) {
    m_hoverIndex = -1;
    m_hoverCloseIndex = -1;
    update();
    QTabBar::leaveEvent(event);
}

}  // namespace traceview
