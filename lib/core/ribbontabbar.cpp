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
    setExpanding(false); // keep every tab at tabSizeHint()'s fixed size, even
                          // when the bar has room to stretch them
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](const ThemePalette&) { update(); });
}

QSize RibbonTabBar::tabSizeHint(int /*index*/) const {
    return QSize(kRibbonTabWidth, kRibbonTabHeight);
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

        painter.setPen(enabled ? (selected ? palette.textPrimary : palette.textSecondary) : palette.textDisabled);
        const int textMargin = kRibbonTabSlant + 4;
        const QString text =
            fontMetrics().elidedText(tabText(i), Qt::ElideRight, static_cast<int>(r.width()) - 2 * textMargin);
        painter.drawText(r, Qt::AlignCenter, text);
    }
}

void RibbonTabBar::mouseMoveEvent(QMouseEvent* event) {
    const int index = tabAt(event->pos());
    if (index != m_hoverIndex) {
        m_hoverIndex = index;
        update();
    }
    QTabBar::mouseMoveEvent(event);
}

void RibbonTabBar::leaveEvent(QEvent* event) {
    m_hoverIndex = -1;
    update();
    QTabBar::leaveEvent(event);
}

} // namespace traceview
