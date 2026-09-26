#pragma once

#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

namespace traceview {

// One entry of a cell header's settings-gear menu (DashboardCell::
// showSettingsMenu()), described by the content widget itself through
// DashboardWidget::viewOptions() -- the cell only knows how to draw a
// checkable action or a select box, never what any option means. That keeps
// DashboardCell free of every widget kind's types, and lets a new chart kind
// get a gear menu just by listing its options (see docs/CHART_STYLE.md).
struct WidgetViewOption {
    enum class Kind { Toggle, Choice };

    QString id;     // stable key, also the widget's config["view"] JSON key
    QString label;  // already translated
    Kind kind = Kind::Toggle;
    // Current value: a bool for Toggle, the selected choice's id for Choice.
    QVariant value;
    // (id, translated label) pairs, Choice only, in menu order.
    QVector<QPair<QString, QString>> choices;
    // Draws a separator above this option -- groups related options.
    bool startsSection = false;
};

}  // namespace traceview
