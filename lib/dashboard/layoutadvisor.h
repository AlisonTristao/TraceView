#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace traceview {

// Layout advice for dashboards: how large each widget kind wants to be,
// where another widget may sit on top of it, and a review of a whole
// dashboard against those rules. Pure (no QWidget, no DashboardGrid): the
// MCP tools (core/agentapptools.cpp, list_widget_types and check_layout)
// and tools/chart_preview feed it plain rectangles. docs/LAYOUT.md is the
// manual.

// How one widget kind likes to be placed. Sizes are in pixels of the
// widget's whole cell (header included) at normal density.
struct WidgetLayoutHints {
    QSizeF minSize;
    QSizeF idealSize;
    QSizeF maxSize;  // 0 in a dimension = no upper limit
    // Width / height range; 0 = any.
    double minAspect = 0.0;
    double maxAspect = 0.0;
    // May sit on top of another widget (a button over a chart).
    bool canOverlay = false;
    // Others may sit on top of it, inside `freeZones`.
    bool acceptsOverlay = false;
    // Fractions (0..1) of the widget's rect where an overlay hides nothing
    // that matters, and where it must not go.
    QVector<QRectF> freeZones;
    QVector<QRectF> keepClear;
    QString note;  // one line for an assistant, in English
};

// The hints for a widget of `typeId` with `config` (a gauge's shape, from
// config["view"]["gaugeShape"], changes them). Unknown kinds get generic
// hints.
WidgetLayoutHints widgetLayoutHints(const QString& typeId, const QJsonObject& config);

// Hints as JSON for an assistant: sizes in pixels and, when `cellSize` is
// given (pixels per grid column and row), in cells too.
QJsonObject widgetLayoutHintsJson(const WidgetLayoutHints& hints, const QSizeF& cellSize = {});

// One widget of a dashboard under review, in pixels of the canvas, in
// stacking order (later = on top, like DashboardGrid's item order).
struct LayoutReviewItem {
    QString id;
    QString name;
    QString typeId;
    QJsonObject config;
    QRectF rect;
};

struct LayoutIssue {
    enum class Severity { Info, Warning, Problem };
    Severity severity = Severity::Warning;
    QString widgetId;
    // "tooSmall", "tooLarge", "aspect", "covers", "alignment", "emptyArea",
    // "offCanvas"
    QString kind;
    QString message;
    // A better rect for the widget, in canvas pixels; null when there's
    // nothing concrete to suggest.
    QRectF suggestedRect;
};

// A free spot where a small control or LED can go on top of a widget.
struct OverlaySpot {
    QString widgetId;
    QRectF rect;  // canvas pixels
};

struct LayoutReview {
    QVector<LayoutIssue> issues;
    QVector<OverlaySpot> overlaySpots;
};

// Reviews `items` on a canvas of `canvasSize` pixels laid out on a grid of
// `gridCells` columns x rows (the Large layout's 60 x 40 by default):
// sizes and aspect against each kind's hints, overlaps (fine only for an
// overlay-capable widget inside a free zone of one that accepts it),
// neighbors whose edges almost but not quite line up, empty areas between
// widgets (the grid's gutter already separates touching widgets, so any
// gap is wasted room), and widgets past the canvas. Also lists the free
// overlay spots still uncovered. The rules are docs/LAYOUT.md's.
LayoutReview reviewLayout(const QVector<LayoutReviewItem>& items, const QSizeF& canvasSize,
                          const QSize& gridCells = QSize(60, 40));

QString layoutSeverityString(LayoutIssue::Severity severity);

}  // namespace traceview
