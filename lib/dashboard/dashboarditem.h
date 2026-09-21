#pragma once

#include <QJsonObject>
#include <QString>

namespace traceview {

// The three screen-size configurations a dashboard can be arranged for --
// phone/tablet/notebook in the UI. DashboardGrid keeps exactly one active at
// a time (see DashboardGrid::setBreakpoint()); every item carries its own
// geometry for all three (see DashboardItem::Geometry below) so each size
// can be laid out independently in Developer mode instead of one layout
// just being rescaled for the others.
enum class DashboardBreakpoint { Small, Medium, Large };

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

    // Position/size as fractions of the canvas, same shape as the old flat
    // x/y/width/height this replaces -- one independent copy per screen-size
    // breakpoint. DashboardGrid is the only thing that reads/writes these
    // directly (via geometry()/setGeometry() below), always for whichever
    // breakpoint is currently active.
    struct Geometry {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };
    Geometry small;
    Geometry medium;
    Geometry large;

    Geometry& geometry(DashboardBreakpoint breakpoint) {
        switch (breakpoint) {
            case DashboardBreakpoint::Small:
                return small;
            case DashboardBreakpoint::Medium:
                return medium;
            case DashboardBreakpoint::Large:
                return large;
        }
        return large;
    }
    const Geometry& geometry(DashboardBreakpoint breakpoint) const {
        return const_cast<DashboardItem*>(this)->geometry(breakpoint);
    }
};

QJsonObject dashboardItemToJson(const DashboardItem& item);

// Returns a default-constructed DashboardItem and sets *ok = false if
// `object` is missing required fields.
DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok);

}  // namespace traceview
