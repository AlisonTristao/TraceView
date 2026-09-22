#pragma once

#include <QRect>
#include <QSize>
#include <QWidget>

class QResizeEvent;
class QPaintEvent;

namespace traceview {

// Device viewport sizes for a manual Phone/Tablet preview (see
// MainWindow::applyBreakpointViewport()) -- rough portrait phone/tablet
// proportions, not any specific real device. Starting defaults, not
// validated against real hardware yet. BOTH dimensions are clamped to
// whatever room this frame actually has (see DevicePreviewFrame::
// layoutContent()) -- a phone's screen is always shown whole, the way a
// real phone's screen is, rather than spilling off a smaller monitor or
// growing a scrollbar of its own; content that needs more room than the
// device's own screen scrolls INSIDE it instead (see MainWindow::
// applyBreakpointViewport()'s canvas-height-multiplier handling, which now
// grows m_dashboardGrid's own minimumHeight rather than this frame's).
const QSize kPhoneViewportSize(360, 700);
// Narrower than an early draft (700x900, which read as too wide/square for
// a "tablet") -- closer to a real Android tablet's portrait aspect ratio
// (~0.6-0.65, e.g. 800x1280) than to a near-square shape.
const QSize kTabletViewportSize(600, 1000);

// Frames the WHOLE app (ribbon, chrome, dashboard/devices content, status
// row -- everything MainWindow's m_appShell holds) inside a viewport
// smaller than the app window itself, standing in for the OS-level window
// resize a manual Phone/Tablet preview used to do (see MainWindow::
// onScreenSizeBreakpointSelected()'s own history) -- and, since this is the
// SAME chrome Android gets (see MainWindow::compactChromeActive()), the
// preview renders literally what a phone/tablet build will show rather than
// a desktop window with a few pieces hidden. Positions its content widget
// directly in resizeEvent() rather than through a QLayout -- same spirit as
// PanelDockController, which positions the floating/docked panels itself
// instead of leaving that to layout machinery that doesn't understand
// "centered, clamped to available space in both axes".
//
// This is the widget MainWindow's central layout actually wraps, in place
// of the ribbon/m_contentRow stack being added there directly; m_appShell
// (holding that stack, plus the new chrome rows) becomes this frame's child
// instead. See docs/DASHBOARD.md's "Screen-size breakpoints" section for
// the full picture.
class DevicePreviewFrame : public QWidget {
    Q_OBJECT

public:
    explicit DevicePreviewFrame(QWidget* parent = nullptr);

    // Reparents `widget` as this frame's sole content. Ownership follows the
    // reparenting, same as QScrollArea::setWidget() -- this frame is now
    // responsible for it.
    void setContentWidget(QWidget* widget);

    // QSize() (invalid, the default) means "no frame": the content widget
    // fills the frame's whole rect(), pixel for pixel -- Notebook. A valid
    // size requests that device's own viewport (Phone/Tablet); BOTH its
    // width and height are clamped to whatever room this frame actually has
    // (see resizeEvent()/layoutContent()) -- the device's screen is always
    // shown whole, like a real phone's screen, never taller or wider than
    // what's actually on screen. Content that needs more room than that
    // scrolls inside the device instead (MainWindow's own dashboard scroll
    // area, now nested inside m_appShell -- see MainWindow::MainWindow()).
    void setDeviceSize(QSize size);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    // Dims the frame's own area outside the device rect and outlines the
    // device with a 1px bezel -- a no-op when there's no frame (Notebook),
    // since the content widget already covers the entire rect() in that
    // case and there's nothing left to draw around.
    void paintEvent(QPaintEvent* event) override;

private:
    // Recomputes m_deviceRect from the current m_deviceSize/geometry and
    // applies it to m_content -- called from setContentWidget(),
    // setDeviceSize(), and resizeEvent() alike, so all three stay in sync
    // through one place.
    void layoutContent();

    QWidget* m_content = nullptr;
    QSize m_deviceSize;  // invalid QSize() = no frame (Notebook)
    // The device's rect in this frame's own coordinates, last computed by
    // layoutContent() -- empty/invalid whenever m_deviceSize is. paintEvent()
    // reads this to know what to leave alone versus dim.
    QRect m_deviceRect;
};

}  // namespace traceview
