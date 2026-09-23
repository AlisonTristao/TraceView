#pragma once

#include <QJsonObject>
#include <QString>
#include <array>

namespace traceview {

// The three screen-size configurations a dashboard can be arranged for --
// phone/tablet/notebook in the UI. DashboardGrid keeps exactly one active at
// a time (see DashboardGrid::setBreakpoint()); every item carries its own
// geometry for all three (see DashboardItem::Geometry below) so each size
// can be laid out independently in Developer mode instead of one layout
// just being rescaled for the others.
enum class DashboardBreakpoint { Small, Medium, Large };

// Number of DashboardBreakpoint members -- drives the size of every
// per-breakpoint array (DashboardItem::geometries, DashboardGrid::
// m_canvasHeightMultiplier) so adding a size only means adding it to the
// enum plus kBreakpointNames in dashboarditem.cpp, not hunting down every
// place a 4th slot would need to be added by hand.
constexpr int kDashboardBreakpointCount = 3;

// Converts DashboardBreakpoint to/from its persisted JSON string ("small"/
// "medium"/"large"). Defined in dashboarditem.cpp; breakpointFromString
// defaults to Large for anything unrecognized -- absent (projects saved
// before per-screen-size layouts existed) or corrupt alike.
QString breakpointToString(DashboardBreakpoint breakpoint);
DashboardBreakpoint breakpointFromString(const QString& value);

// True for the two breakpoints that get a device-frame preview and
// growable canvas height (Small/Medium) -- false for Large, whose canvas
// just matches the viewport like a notebook window being resized normally.
// Centralizes what used to be scattered `!= DashboardBreakpoint::Large`/
// `== DashboardBreakpoint::Large` comparisons in dashboardgrid.cpp.
constexpr bool isPreviewBreakpoint(DashboardBreakpoint bp) {
    return bp != DashboardBreakpoint::Large;
}

// Ceiling for how many viewport heights ("pages") a Small/Medium canvas can
// be grown to (see DashboardGrid::growCanvasHeight()) -- also the upper
// bound a Small/Medium item's y/height is clamped to on load, since those
// are measured in pages rather than fractions of the whole canvas (see
// DashboardItem::Geometry).
constexpr double kMaxCanvasPages = 8.0;

// Where one dashboard widget sits on the grid: its registered type and its
// position/size as fractions (0.0-1.0) of the grid's usable canvas area.
// Proportional coordinates keep layouts resolution-independent — an item
// stays at the same relative spot/size across resizes with no clamping or
// reflow needed. Plain data — the grid owns the actual QWidget and is the
// only place that converts these fractions to pixels.
struct DashboardItem {
    QString id;          // QUuid, stable identity across the item's lifetime
    QString typeId;      // key into WidgetRegistry
    QString name;        // user-editable display name; empty falls back to the
                         // type's default display name (see displayNameFor())
    QString key;         // user-editable, must be unique when non-empty (see
                         // DashboardGrid::isKeyAvailable); the future handle
                         // external data updates will target, independent of
                         // `id` (which stays internal/auto-generated) and of
                         // `name`/`typeId` (either of which can change freely)
    QJsonObject config;  // type-specific settings edited via the properties
                         // panel's WidgetConfigEditor for `typeId`; opaque to
                         // everything except that editor, empty for types
                         // that don't register one
    QString groupId;     // empty = ungrouped; items sharing a non-empty value
                         // always select/drag together as one rigid unit (see
                         // DashboardGrid::groupSelected()/ungroupSelected())

    // Position/size, one independent copy per screen-size breakpoint. x/width
    // are always fractions of the canvas width. y/height are fractions of one
    // "page" -- the viewport's own height: for Large that is the whole canvas
    // (0.0-1.0, same as always), while a Small/Medium canvas grown past the
    // viewport (see DashboardGrid::growCanvasHeight()) spans 0.0 up to its
    // page count, so growing it adds room below instead of stretching every
    // item taller. DashboardGrid is the only thing that reads/writes these
    // directly (via geometry() below).
    struct Geometry {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };
    // Indexed by DashboardBreakpoint (see geometry() below) instead of one
    // named member per size -- adding a 4th breakpoint only means bumping
    // kDashboardBreakpointCount, not adding a matching member here too.
    std::array<Geometry, kDashboardBreakpointCount> geometries;

    Geometry& geometry(DashboardBreakpoint breakpoint) {
        return geometries[size_t(breakpoint)];
    }
    const Geometry& geometry(DashboardBreakpoint breakpoint) const {
        return geometries[size_t(breakpoint)];
    }
};

QJsonObject dashboardItemToJson(const DashboardItem& item);

// Returns a default-constructed DashboardItem and sets *ok = false if
// `object` is missing required fields.
DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok);

}  // namespace traceview
