#include "devicepreviewframe.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

#include "traceview/thememanager.h"

namespace traceview {

namespace {
// Breathing room around the device rect, on every side, whenever a frame is
// active (Phone/Tablet) -- keeps the bezel outline from landing flush
// against this frame's own edge, and is subtracted from the available space
// in both axes before clamping the device to it (see layoutContent()).
constexpr int kDeviceMargin = 24;
}  // namespace

DevicePreviewFrame::DevicePreviewFrame(QWidget* parent) : QWidget(parent) {}

void DevicePreviewFrame::setContentWidget(QWidget* widget) {
    m_content = widget;
    if (m_content) {
        m_content->setParent(this);
        m_content->show();
    }
    layoutContent();
}

void DevicePreviewFrame::setDeviceSize(QSize size) {
    if (m_deviceSize == size) {
        return;
    }
    m_deviceSize = size;
    layoutContent();
}

QSize DevicePreviewFrame::sizeHint() const {
    return minimumSizeHint();
}

QSize DevicePreviewFrame::minimumSizeHint() const {
    // No longer derives extra height (or width) from m_deviceSize -- BOTH of
    // the device's own dimensions are clamped to whatever this frame is
    // actually given (see layoutContent()), the same way a real phone's
    // screen never grows past its own bezel. Content that needs more room
    // scrolls INSIDE the device instead (MainWindow's own dashboard scroll
    // area, now nested inside m_appShell -- see MainWindow::
    // applyBreakpointViewport()'s canvas-height-multiplier handling). Same
    // modest floor DashboardGrid::contentSize() itself falls back to.
    constexpr QSize kFloor(320, 240);
    return kFloor;
}

void DevicePreviewFrame::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutContent();
}

void DevicePreviewFrame::layoutContent() {
    if (!m_content) {
        return;
    }

    if (!m_deviceSize.isValid()) {
        // Notebook/User mode: no frame around the content -- it fills this
        // frame's whole rect() exactly, the same as it filled central's own
        // layout directly before this class framed the whole app.
        m_deviceRect = QRect();
        m_content->setGeometry(rect());
        update();
        return;
    }

    // Phone/Tablet: center in BOTH axes, clamping BOTH dimensions to
    // whatever room this frame actually has -- never the device's own
    // nominal size unconditionally -- so the device's screen is always
    // shown whole, the way a real phone's screen is, instead of spilling
    // off a smaller window or growing a scrollbar of its own. A device
    // that needs more vertical room than its own screen scrolls INSIDE it
    // instead (MainWindow's dashboard scroll area, nested inside m_appShell
    // -- see MainWindow::applyBreakpointViewport()). Independent per-axis
    // clamps, not a uniform scale-down -- same spirit as the width-only
    // clamp this used to be, just applied to height too now that the
    // device's own height no longer varies (see minimumSizeHint()).
    const int availableWidth = qMax(1, width() - 2 * kDeviceMargin);
    const int availableHeight = qMax(1, height() - 2 * kDeviceMargin);
    const int deviceWidth = qMin(m_deviceSize.width(), availableWidth);
    const int deviceHeight = qMin(m_deviceSize.height(), availableHeight);
    const int x = (width() - deviceWidth) / 2;
    const int y = (height() - deviceHeight) / 2;
    m_deviceRect = QRect(x, y, deviceWidth, deviceHeight);
    m_content->setGeometry(m_deviceRect);
    update();
}

void DevicePreviewFrame::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    if (!m_deviceRect.isValid()) {
        // No frame: the content widget already covers the whole rect()
        // (see layoutContent()), nothing here to dim or outline.
        return;
    }

    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    // ThemePalette (traceview/theme.h) has no dedicated "dimmed surrounding"
    // token, so this derives one from @background@ itself rather than
    // inventing a fixed color -- keeps a custom theme (docs/THEMING.md:
    // "adding a new one requires no other code changes") looking coherent
    // here for free. Darkening reads as "receded" against most themes, but
    // a background that's already near-black can't visibly darken any
    // further, so it lightens slightly there instead.
    const QColor bg = palette.background;
    const QColor scrim = bg.lightness() < 80 ? bg.lighter(130) : bg.darker(115);

    // The base QWidget background (the global "background-color:
    // @background@" QSS rule in stylesheet.cpp) already fills this frame's
    // whole rect() before paintEvent() ever runs -- only the area outside
    // the device needs dimming, not the device rect itself, which the
    // content widget (a child, painted on top of anything drawn here)
    // covers regardless.
    QRegion surrounding(rect());
    surrounding -= m_deviceRect;
    painter.setClipRegion(surrounding);
    painter.fillRect(rect(), scrim);
    painter.setClipping(false);

    // 1px bezel outline around the device -- same border token the rest of
    // lib/core uses for subtle dividers (see ThemePalette::border).
    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_deviceRect.adjusted(0, 0, -1, -1));
}

}  // namespace traceview
