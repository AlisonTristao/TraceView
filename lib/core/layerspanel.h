#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace traceview {

struct DashboardLayerEntry;

// Checklist of every item currently on the dashboard grid, front-most first
// -- lets a widget that ends up hidden behind another (now that overlapping
// placement is allowed, see DashboardGrid::isPlacementValid()) stay
// selectable instead of getting lost/forgotten underneath. Shown to the left
// of the canvas, at 1/3 of PropertiesPanel's width on the right.
//
// Dumb like PropertiesPanel/Ribbon -- MainWindow feeds it state via
// setItems() and reacts to itemSelected() by calling
// DashboardGrid::selectItem(); this widget never touches DashboardGrid
// directly. Selecting a row is exactly the same "temporarily bring to
// front, restore its own layer on deselect" behavior canvas clicks get,
// since both funnel through that one DashboardGrid::selectItem() call. The
// pin toggle in the top-right corner is the one exception -- see
// PropertiesPanel's class comment, this panel owns it the same way.
class LayersPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayersPanel(QWidget* parent = nullptr);

    // entries in DashboardGrid::layerEntries()'s back-to-front order; shown
    // reversed (front-most first). selectedId highlights the matching row
    // (empty clears the highlight). Never emits itemSelected().
    void setItems(const QVector<DashboardLayerEntry>& entries, const QString& selectedId);

    // Whether the pin toggle is engaged -- MainWindow keeps the panel visible
    // even with no selection while this is true.
    bool isPinned() const { return m_pinned; }

signals:
    // Empty when the list's current row is cleared (e.g. a click that lands
    // on blank space below the last row).
    void itemSelected(const QString& itemId);
    void pinnedChanged(bool pinned);

private:
    void onCurrentItemChanged(QListWidgetItem* current);
    void onPinToggled(bool checked);
    void updatePinIcon();

    // Guards onCurrentItemChanged() while setItems() rebuilds rows, so
    // resyncing the list from DashboardGrid state doesn't loop back into an
    // itemSelected() emission.
    bool m_syncing = false;
    QListWidget* m_list = nullptr;

    bool m_pinned = false;
    QToolButton* m_pinButton = nullptr;
};

} // namespace traceview
