#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace traceview {

// A named set of color tokens. A "template" in the UI is just one of these —
// adding a new one requires no other code changes, see docs/THEMING.md.
struct ThemePalette {
    QString id;           // stable key, e.g. "dark"
    QString displayName;  // shown in the theme picker, e.g. "Dark"

    QColor background;   // window background
    QColor surface;      // panels, cards
    QColor surfaceAlt;   // hovered/alternate rows, input fields

    QColor border;        // subtle dividers
    QColor borderStrong;  // emphasis borders, focus outlines

    QColor textPrimary;
    QColor textSecondary;
    QColor textDisabled;

    QColor accent;
    QColor accentHover;
    QColor accentPressed;

    QColor success;
    QColor warning;
    QColor danger;

    // Reserved for future telemetry plots (one color per data series).
    QVector<QColor> series;
};

} // namespace traceview
