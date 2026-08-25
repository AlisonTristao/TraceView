#pragma once

#include <QString>
#include <QVector>

#include "dockablepanel.h"

class QListWidget;
class QListWidgetItem;

namespace traceview {

struct DashboardLayerEntry;

// Checklist of every item currently on the dashboard grid, front-most first
// -- lets a widget that ends up hidden behind another (now that overlapping
// placement is allowed, see DashboardGrid::isPlacementValid()) stay
// selectable instead of getting lost/forgotten underneath. Docked to the
// left of the canvas by default, at 1/3 of PropertiesPanel's width on the
// right -- see preferredThickness().
//
// Dumb like PropertiesPanel/Ribbon -- MainWindow feeds it state via
// setItems() and reacts to itemSelected() by calling
// DashboardGrid::selectItem(); this widget never touches DashboardGrid
// directly. Selecting a row is exactly the same "temporarily bring to
// front, restore its own layer on deselect" behavior canvas clicks get,
// since both funnel through that one DashboardGrid::selectItem() call.
// Header/pin toggle/drag-to-dock behavior lives in DockablePanel, this
// class only owns the list itself.
class LayersPanel : public DockablePanel {
    Q_OBJECT

public:
    explicit LayersPanel(QWidget* parent = nullptr);

    // entries in DashboardGrid::layerEntries()'s back-to-front order; shown
    // reversed (front-most first). selectedId highlights the matching row
    // (empty clears the highlight). Never emits itemSelected().
    void setItems(const QVector<DashboardLayerEntry>& entries, const QString& selectedId);

    int preferredThickness() const override;

signals:
    // Empty when the list's current row is cleared (e.g. a click that lands
    // on blank space below the last row).
    void itemSelected(const QString& itemId);

private:
    void onCurrentItemChanged(QListWidgetItem* current);

    // Guards onCurrentItemChanged() while setItems() rebuilds rows, so
    // resyncing the list from DashboardGrid state doesn't loop back into an
    // itemSelected() emission.
    bool m_syncing = false;
    QListWidget* m_list = nullptr;
};

}  // namespace traceview
