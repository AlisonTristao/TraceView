#pragma once

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "dashboard/widgets/controlstyle.h"

class QPainter;

namespace traceview {

// The control paint building blocks: every control widget
// (widgets/controlwidgets.h) draws itself through these, reading the look
// from a ControlStyle (controlstyle.h) -- the controls' counterpart of
// chartpainting.h. A paint function never reads the theme itself, and never
// branches on the style id: it switches on the look token it draws.
// Every function draws the control greyed out when `enabled` is false.

// Fonts and colors of a control's text.
QFont controlTextFont(const ControlStyle& style, const QFont& base);
QFont controlTitleFont(const ControlStyle& style, const QFont& base);
QColor controlTitleColor(const ControlStyle& style, const ControlColors& colors);
QColor controlStateTextColor(const ControlStyle& style, const ControlColors& colors);

// A push button filling `rect`.
void paintControlButton(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                        const ControlColors& colors, const QFont& font, const QString& text,
                        ControlTone tone, bool down, bool hovered, bool enabled);

// A switch at its natural size, `t` 0 (off) .. 1 (on) -- fractional while
// it animates. `offText`/`onText` are used by looks with text cells.
QSizeF controlSwitchSize(const ControlStyle& style);
void paintControlSwitch(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                        const ControlColors& colors, const QFont& font, qreal t, bool enabled,
                        const QString& offText, const QString& onText);

// Where a slider's value runs inside `rect`: from `left` (minimum) to
// `right` (maximum), on the line `y`. The same geometry the painter uses,
// so a click lands where the thumb is drawn.
struct ControlSliderTrack {
    qreal left = 0.0;
    qreal right = 0.0;
    qreal y = 0.0;
};

qreal controlSliderHeight(const ControlStyle& style);
ControlSliderTrack controlSliderTrack(const QRectF& rect, const ControlStyle& style);
// 0..1 position of `x` along `track`, clamped.
qreal controlSliderFractionAt(const ControlSliderTrack& track, qreal x);
// `scaleLabels` (min, middle, max) are printed under looks with a scale.
void paintControlSlider(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                        const ControlColors& colors, const QFont& font, qreal fraction, bool down,
                        bool enabled, const QStringList& scaleLabels);

// A value readout (the slider's value box). `widest` is the longest text
// it will show, so the box doesn't change size while the value moves.
QSizeF controlReadoutSize(const ControlStyle& style, const QFont& font, const QString& widest);
void paintControlReadout(QPainter& painter, const QRectF& rect, const ControlStyle& style,
                         const ControlColors& colors, const QFont& font, const QString& text,
                         bool enabled);

// An LED of lamp radius `radius` centred on `center`. Halos and bezels
// reach out to about 1.6 x radius.
inline constexpr qreal kControlLedReach = 1.6;
void paintControlLed(QPainter& painter, const QPointF& center, qreal radius,
                     const ControlStyle& style, const ControlColors& colors, const QColor& color,
                     bool on, bool enabled);

}  // namespace traceview
