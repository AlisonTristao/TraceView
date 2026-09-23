#pragma once

#include <QByteArray>
#include <QColor>
#include <QWidget>

class QSvgRenderer;

namespace traceview {

// The TraceView wordmark (resources/branding/traceview-wordmark-corner-cropped.svg,
// via branding.qrc)
// pinned to its parent's top-right corner, User mode only (see
// MainWindow::updateChromeVisibility()). The corner variant's main trace runs
// past the SVG's right edge, so with this widget flush against the window
// edge the signal reads as coming in from off-screen. MainWindow lines that
// trace up with the border between the top row and the dashboard.
//
// Purely decorative: an overlay child rather than a layout item -- it is
// taller than the ribbon row it sits on and overhangs the top of the
// dashboard -- and transparent to the mouse, so whatever it covers stays
// clickable. The wordmark's light strokes/letters follow the theme's
// textPrimary (they would vanish on a light theme otherwise); the brand
// blue stays fixed.
class BrandCornerMark : public QWidget {
    Q_OBJECT

public:
    explicit BrandCornerMark(QWidget* parent = nullptr);

    // Re-tints the light parts of the mark; call on every theme change.
    void setForegroundColor(const QColor& color);
    // Sizes to `height` (width follows the SVG's aspect ratio, capped at
    // `maxWidth`) and moves flush against the parent's right edge, with the
    // wordmark's middle (white) trace centered on parent y `midlineY`. Shrinks
    // further if needed so the top never leaves the parent.
    void reposition(int height, int maxWidth, int midlineY);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QByteArray m_svgTemplate;
    QSvgRenderer* m_renderer = nullptr;
};

}  // namespace traceview
