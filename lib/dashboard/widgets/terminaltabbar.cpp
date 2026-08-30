#include "terminaltabbar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kSlant = 10;        // horizontal run of each slanted side
constexpr int kTabHeight = 26;
constexpr int kHPad = 12;         // gap between the slant and the content
constexpr int kDotSize = 7;
constexpr int kDotTextGap = 6;
constexpr int kTextSlack = 8;       // a little breathing room so short names never clip
constexpr int kMaxTextWidth = 160;  // long device names elide past this

}  // namespace

TerminalTabBar::TerminalTabBar(QWidget* parent) : QTabBar(parent) {
    setMouseTracking(true);
    setExpanding(false);
    setDrawBase(false);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

void TerminalTabBar::setTabConnected(int index, bool connected) {
    if (index < 0 || index >= count()) {
        return;
    }
    setTabData(index, connected);
    update();
}

QSize TerminalTabBar::tabSizeHint(int index) const {
    const int textW = qMin(fontMetrics().horizontalAdvance(tabText(index)), kMaxTextWidth);
    return QSize(2 * kSlant + 2 * kHPad + kDotSize + kDotTextGap + textW + kTextSlack, kTabHeight);
}

QSize TerminalTabBar::minimumTabSizeHint(int index) const {
    // Enough for the dot plus an ellipsis; the label elides above this.
    Q_UNUSED(index);
    return QSize(2 * kSlant + 2 * kHPad + kDotSize + kDotTextGap + 16, kTabHeight);
}

void TerminalTabBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    painter.fillRect(rect(), palette.background);

    for (int i = 0; i < count(); ++i) {
        // Trim 2px off the right edge so adjacent trapezoids read as separate
        // folder tabs rather than one fused strip (same as RibbonTabBar).
        const QRectF r = QRectF(tabRect(i)).adjusted(0, 0, -2, 0);
        const bool selected = i == currentIndex();

        QPainterPath path;
        path.moveTo(r.left() + kSlant, r.top());
        path.lineTo(r.right() - kSlant, r.top());
        path.lineTo(r.right(), r.bottom());
        path.lineTo(r.left(), r.bottom());
        path.closeSubpath();

        QColor fill = selected ? palette.surfaceAlt : palette.surface;
        if (!selected && i == m_hoverIndex) {
            fill = palette.surfaceAlt;
        }

        painter.setPen(QPen(palette.border, 1));
        painter.setBrush(fill);
        painter.drawPath(path);

        if (selected) {
            // Merge the selected tab into the terminal painted directly below.
            painter.setPen(QPen(fill, 1));
            painter.drawLine(QPointF(r.left() + 1, r.bottom()), QPointF(r.right() - 1, r.bottom()));
        }

        QRectF contentRect = r.adjusted(kSlant + kHPad, 0, -(kSlant + kHPad), 0);

        const QRectF dotRect(contentRect.left(), contentRect.center().y() - kDotSize / 2.0, kDotSize,
                             kDotSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tabData(i).toBool() ? palette.success : palette.danger);
        painter.drawEllipse(dotRect);
        contentRect.setLeft(dotRect.right() + kDotTextGap);

        painter.setPen(selected ? palette.textPrimary : palette.textSecondary);
        const QString text = fontMetrics().elidedText(tabText(i), Qt::ElideRight,
                                                      qMax(0, int(contentRect.width())));
        painter.drawText(contentRect, Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

void TerminalTabBar::mouseMoveEvent(QMouseEvent* event) {
    const int index = tabAt(event->pos());
    if (index != m_hoverIndex) {
        m_hoverIndex = index;
        update();
    }
    QTabBar::mouseMoveEvent(event);
}

void TerminalTabBar::leaveEvent(QEvent* event) {
    m_hoverIndex = -1;
    update();
    QTabBar::leaveEvent(event);
}

}  // namespace traceview
