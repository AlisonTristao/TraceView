#include "terminaltabbar.h"

#include <QMouseEvent>
#include <QPainter>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kTabHeight = 26;
constexpr int kHPad = 12;            // padding between the tab edge and the label
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

int TerminalTabBar::uniformTextWidth() const {
    int widest = 0;
    for (int i = 0; i < count(); ++i) {
        widest = qMax(widest, fontMetrics().horizontalAdvance(tabText(i)));
    }
    return qMin(widest, kMaxTextWidth);
}

QSize TerminalTabBar::tabSizeHint(int index) const {
    Q_UNUSED(index);
    return QSize(2 * kHPad + uniformTextWidth() + kTextSlack, kTabHeight);
}

QSize TerminalTabBar::minimumTabSizeHint(int index) const {
    return tabSizeHint(index);
}

void TerminalTabBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    painter.fillRect(rect(), palette.background);

    for (int i = 0; i < count(); ++i) {
        const QRect r = tabRect(i);
        const bool selected = i == currentIndex();

        QColor fill = selected ? palette.surfaceAlt : palette.surface;
        if (!selected && i == m_hoverIndex) {
            fill = palette.surfaceAlt;
        }
        painter.fillRect(r, fill);

        // 1px cell borders: top + right on every tab, left on the first, and a
        // bottom edge on the unselected ones -- the selected tab is left open
        // so it reads as continuous with the terminal painted below.
        painter.setPen(palette.border);
        painter.drawLine(r.topLeft(), r.topRight());
        painter.drawLine(r.topRight(), r.bottomRight());
        if (i == 0) {
            painter.drawLine(r.topLeft(), r.bottomLeft());
        }
        if (!selected) {
            painter.drawLine(r.bottomLeft(), r.bottomRight());
        }

        const QRect textRect = r.adjusted(kHPad, 0, -kHPad, 0);
        painter.setPen(selected ? palette.textPrimary : palette.textSecondary);
        const QString text =
            fontMetrics().elidedText(tabText(i), Qt::ElideRight, qMax(0, textRect.width()));
        painter.drawText(textRect, Qt::AlignCenter, text);
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
