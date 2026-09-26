#pragma once

#include <QString>
#include <QVector>

namespace traceview {

// Axis tick math shared by every chart kind -- pure functions, no QWidget
// and no painting, so they stay unit-testable (tests/test_chartscale.cpp).
// See docs/CHART_STYLE.md for how the chart styles use them.

// Round-number ticks for one axis: every multiple of `step` inside
// [lo, hi], where `step` is 1, 2, 2.5 or 5 times a power of ten -- the same
// "nice numbers" matplotlib's MaxNLocator and MATLAB's default axis ticks
// use, so an axis reads 0/20/40/60 instead of 0.37/49.81/99.25.
struct NiceScale {
    double lo = 0.0;    // axis range actually used -- see niceScale()'s `expand`
    double hi = 1.0;
    double step = 1.0;  // distance between adjacent ticks
    QVector<double> ticks;
    int decimals = 0;   // fractional digits every tick label needs, see tickDecimals()
};

// Picks the densest nice step that still yields at most `maxTicks` ticks
// over [lo, hi] (maxTicks is clamped to >= 2).
//   expand == true:  lo/hi grow outward to the nearest ticks, so the first
//                    and last tick sit exactly on the axis ends -- MATLAB's
//                    "axis auto", used for auto-ranged data.
//   expand == false: lo/hi stay as given and only the ticks inside them are
//                    returned -- for a fixed range or a device-declared one,
//                    which must not be widened.
// A degenerate range (lo == hi, or either one non-finite) is widened around
// its value first, so the result always has hi > lo and at least one tick.
NiceScale niceScale(double lo, double hi, int maxTicks, bool expand);

// Fewest fractional digits that print `step` (and so every multiple of it)
// exactly: 20 -> 0, 2.5 -> 1, 0.25 -> 2. Capped at 9.
int tickDecimals(double step);

// A tick value formatted with `decimals` fixed digits, with "-0" folded to
// "0" (a tick computed as -0.0000001 must not print a minus sign).
QString formatTick(double value, int decimals);

// How many ticks fit along an axis `lengthPx` long when each tick needs
// `spacingPx` of room (label size plus breathing space) -- clamped to
// [2, 11], the range any readable axis stays within.
int maxTicksFor(int lengthPx, int spacingPx);

}  // namespace traceview
