#pragma once

#include <optional>

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

class QEvent;
class QWidget;

namespace traceview {

class DockablePanel;
class DockDropIndicator;
class DockResizeGrip;

// Where a panel currently sits. Left/Right span m_contentRow's full height at
// a fixed (but user-resizable, see DockResizeGrip) width; Top/Bottom span its
// full width at a fixed height. Floating means the panel is its own
// top-level window, positioned (and sized) in screen coordinates instead of
// relative to m_contentRow.
enum class DockEdge { Left, Right, Top, Bottom, Floating };

// Owns the custom drag-and-drop docking behavior shared by LayersPanel/
// PropertiesPanel: both panels always overlay the canvas (m_contentRow's
// layout is never touched, so DashboardGrid never resizes because of this --
// see MainWindow's original positionOverlayPanels() comment for why that
// matters), but can now be dragged to any of m_contentRow's four edges or
// pulled off entirely into a floating window, and resized once there. This
// intentionally does not use QMainWindow/QDockWidget: that always reserves
// layout space for a docked widget, which is exactly the canvas-resize
// behavior this is meant to avoid.
//
// Each DockablePanelHeader (see dockablepanelheader.h) only reports raw
// mouse-drag events; this class is the one place that turns a drag into an
// edge decision, drives the drop-zone indicator, and applies geometry -- so
// that logic exists once instead of once per panel. DockResizeGrip is the
// same idea applied to resizing: one grip per panel reports raw drag deltas,
// this class decides what they mean (docked thickness vs. floating size).
class PanelDockController : public QObject {
    Q_OBJECT

public:
    // contentRow is the area panels dock within (MainWindow's m_contentRow);
    // window is the panels' floating-window parent (MainWindow itself, so a
    // floating panel is destroyed along with it, same ownership idiom as
    // DebugChartsWindow).
    PanelDockController(QWidget* contentRow, QWidget* window, QObject* parent = nullptr);

    // settingsId is a stable per-panel key for persistence (e.g. "layers");
    // defaultEdge is applied verbatim when nothing was saved yet. Panels must
    // already be children of contentRow when this is called.
    void registerPanel(DockablePanel* panel, const QString& settingsId, DockEdge defaultEdge);

    // Re-applies geometry to every currently-docked (non-floating) panel, and
    // syncs each panel's resize grip to match -- call whenever contentRow
    // resizes. Floating panels are untouched (their geometry is independent,
    // screen-relative).
    void relayout();

    // Reads each registered panel's saved edge/thickness/geometry from
    // QSettings (see saveState()) and applies it, falling back to the
    // defaultEdge/preferredThickness() passed to/declared by registerPanel()
    // for a panel with nothing saved yet. Call once, after every
    // registerPanel() call.
    void restoreState();

    // True while a header-initiated move or a grip-initiated resize is in
    // progress -- MainWindow uses this to avoid hiding a panel (via
    // updatePanelVisibility()) out from under an in-progress gesture.
    bool isDragging() const { return m_draggingPanel != nullptr || m_resizingPanel != nullptr; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    // Emitted after a move or resize gesture ends and the new state has been
    // persisted -- MainWindow re-runs updatePanelVisibility() off this, in
    // case a selection/tab change landed mid-gesture.
    void dragFinished();

private:
    struct PanelState {
        DockEdge edge = DockEdge::Left;
        QString settingsId;
        // Current docked width (Left/Right) or height (Top/Bottom); also
        // what a floating panel is sized to the first time it's ever
        // floated. Starts at the panel's preferredThickness(), adjustable
        // via its edge grip.
        int thickness = 0;
        // Strip along the panel's inner (canvas-facing) edge, child of
        // m_contentRow, visible only while docked -- see updateEdgeGripGeometry().
        DockResizeGrip* edgeGrip = nullptr;
        // Corner square, child of the panel itself, visible only while
        // floating -- see updateCornerGripGeometry().
        DockResizeGrip* cornerGrip = nullptr;
    };

    void onDragStarted(DockablePanel* panel, QPoint globalPos);
    void onDragMoved(DockablePanel* panel, QPoint globalPos);
    void onDragFinished(DockablePanel* panel, QPoint globalPos);

    void onEdgeResizeStarted(DockablePanel* panel, QPoint globalPos);
    void onEdgeResizeMoved(DockablePanel* panel, QPoint globalPos);
    void onEdgeResizeFinished(DockablePanel* panel, QPoint globalPos);
    void onCornerResizeStarted(DockablePanel* panel, QPoint globalPos);
    void onCornerResizeMoved(DockablePanel* panel, QPoint globalPos);
    void onCornerResizeFinished(DockablePanel* panel, QPoint globalPos);

    // nullopt when globalPos isn't close enough to a dockable edge (outside
    // contentRow entirely, or over the canvas but not near its border) --
    // dropping there leaves the panel floating exactly where it was released.
    std::optional<DockEdge> edgeForGlobalPos(QPoint globalPos) const;
    // Geometry (in contentRow-local coordinates) `panel` would occupy if
    // docked to `edge` right now, accounting for any other panel already
    // docked to the same edge (they stack in registration order, extending
    // away from the edge).
    QRect geometryForEdge(DockEdge edge, DockablePanel* panel) const;
    QRect defaultFloatingGeometry(DockablePanel* panel) const;
    // Keeps `desired` from shrinking a panel below a usable minimum or
    // growing it past what's left after every other panel sharing `edge`
    // (and a little breathing room for the canvas) has taken its share.
    int clampThickness(DockEdge edge, DockablePanel* panel, int desired) const;

    void dockPanel(DockablePanel* panel, DockEdge edge);
    void saveState(DockablePanel* panel) const;

    // Shows/hides + repositions `panel`'s edge or corner grip to match its
    // current dock state and visibility -- called after anything that could
    // have changed either (relayout(), a dock/float transition, or the
    // panel's own visibility flipping via MainWindow's eventFilter()).
    void updateGripVisibility(DockablePanel* panel);
    void updateEdgeGripGeometry(DockablePanel* panel);
    void updateCornerGripGeometry(DockablePanel* panel);

    QWidget* m_contentRow;
    QWidget* m_window;
    QHash<DockablePanel*, PanelState> m_states;
    // Registration order -- also the deterministic stacking order used when
    // two panels share an edge.
    QVector<DockablePanel*> m_dockOrder;
    DockDropIndicator* m_indicator;

    DockablePanel* m_draggingPanel = nullptr;
    QPoint m_grabOffset;
    std::optional<DockEdge> m_candidateEdge;

    DockablePanel* m_resizingPanel = nullptr;
    bool m_resizingCorner = false;
    QPoint m_lastResizePos;
};

} // namespace traceview
