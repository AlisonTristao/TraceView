#pragma once

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

namespace traceview {

// The frame style sheet: the shape and outline of every large custom-painted
// container (dashboard cells, the widgets inside them, device cards) and the
// corner radius of QSS-driven controls. Picked app-wide from View > Frame
// (also Settings > Appearance) and owned by ThemeManager, next to the
// palette: the palette decides colors, the frame decides shapes. See "Frame
// styles" in docs/VISUAL_IDENTITY.md.
//
//   Rounded     -- the original look: 6px rounded corners, 2px outline.
//   Square      -- sharp corners and a 1px outline, controls square too.
//   Borderless  -- rounded corners, no idle outline: a card reads only by
//                  its fill against the canvas (selection still outlines).
//   Chamfered   -- corners cut at 45 degrees, 2px outline, square controls.

enum class FrameStyleId { Rounded, Square, Borderless, Chamfered };

enum class FrameCorner { Round, Square, Chamfer };

struct FrameStyle {
    FrameStyleId id = FrameStyleId::Rounded;
    FrameCorner corner = FrameCorner::Round;
    // Radius for Round, length of the cut along each edge for Chamfer.
    qreal cornerSize = 6.0;
    // Idle outline around a card; 0 together with idleOutline == false for
    // Borderless. The selection outline keeps its own fixed width.
    qreal borderWidth = 2.0;
    bool idleOutline = true;
    // Multiplies every border-radius in the app stylesheet (buttons, inputs,
    // combo boxes...): 1 keeps them rounded, 0 makes them square.
    double controlRadiusScale = 1.0;
};

// "rounded"/"square"/"borderless"/"chamfered" -- the QSettings value and
// menu data. FromId() falls back to Rounded.
QString frameStyleIdString(FrameStyleId id);
FrameStyleId frameStyleFromId(const QString& id);
QString frameStyleDisplayName(FrameStyleId id);  // translated
QVector<FrameStyleId> allFrameStyles();
const FrameStyle& frameStyle(FrameStyleId id);

// Width of the selection outline cells and cards draw on top, whatever the
// frame style -- selection must stay obvious even on a borderless frame.
constexpr qreal kFrameSelectionWidth = 2.0;

// A rounded rect where any corner can be forced square instead. Built by
// unioning a fully-rounded path with a square patch over each corner that
// should stay sharp (path.simplified() merges the overlapping subpaths into
// one clean outline/region) rather than hand-rolling QPainterPath::arcTo
// signs per corner.
QPainterPath partiallyRoundedRect(const QRectF& r, qreal radius, bool roundTopLeft,
                                  bool roundTopRight, bool roundBottomLeft,
                                  bool roundBottomRight);

// `r` in `style`'s corner shape. A corner passed as false stays square --
// e.g. a widget's top corners, which meet its cell header at a straight
// seam instead of the outer outline.
QPainterPath frameShapePath(const QRectF& r, const FrameStyle& style, bool topLeft = true,
                            bool topRight = true, bool bottomLeft = true,
                            bool bottomRight = true);

// frameShapePath() in the app's current frame style (ThemeManager).
QPainterPath currentFramePath(const QRectF& r, bool topLeft = true, bool topRight = true,
                              bool bottomLeft = true, bool bottomRight = true);

// `css` with every "border-radius: Npx" (and its per-corner variants)
// multiplied by `scale`, rounded to whole pixels. How the frame style
// reaches the QSS controls without the stylesheet builder knowing about it.
QString scaleStyleSheetRadii(const QString& css, double scale);

}  // namespace traceview
