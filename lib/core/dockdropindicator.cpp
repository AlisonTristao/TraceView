#include "dockdropindicator.h"

#include <QPainter>

#include "traceview/thememanager.h"

namespace traceview {

namespace {
// Faint enough that the canvas underneath (and whatever's docked already)
// stays readable through it -- this is a preview, not an occlusion.
constexpr int kFillAlpha = 70;
constexpr int kBorderAlpha = 160;
} // namespace

DockDropIndicator::DockDropIndicator(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    hide();
}

void DockDropIndicator::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QColor accent = ThemeManager::instance().currentTheme().accent;

    QColor fill = accent;
    fill.setAlpha(kFillAlpha);
    QColor border = accent;
    border.setAlpha(kBorderAlpha);

    painter.setPen(QPen(border, 2));
    painter.setBrush(fill);
    painter.drawRect(rect().adjusted(1, 1, -1, -1));
}

} // namespace traceview
