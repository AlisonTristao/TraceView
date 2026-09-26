#pragma once

#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVariantAnimation>
#include <QWidget>

#include "dashboard/dashboardwidget.h"

class QEnterEvent;

namespace traceview {


// Chrome wrapped around one DashboardWidget on the grid. The header strip
// (icon + title) is always visible, in both Layout and Run. In edit mode,
// clicking an unselected cell just selects it (only one selected at a
// time, grid-wide — DashboardGrid enforces that); the selected cell's
// header also doubles as a drag handle and gains a resize grip, unselected
// cells stay flat. This widget does no grid math itself — it only reports
// raw cursor positions via signals; DashboardGrid owns cell-size/collision
// logic and decides where the cell actually ends up.
class DashboardCell : public QWidget {
    Q_OBJECT

public:
    // Which part of the border was grabbed. The 4 edges resize a single
    // dimension (anchoring the opposite edge in place); the 4 corners
    // resize both dimensions at once.
    enum class ResizeHandle {
        None,
        Top,
        Bottom,
        Left,
        Right,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    // typeId is only used to pick a small header icon (see drawTypeIcon() in
    // the .cpp) — the cell doesn't otherwise care what kind of widget it
    // wraps.
    DashboardCell(const QString& itemId, const QString& typeId, const QString& title,
                  DashboardWidget* content, QWidget* parent = nullptr);

    QString itemId() const {
        return m_itemId;
    }
    DashboardWidget* content() const {
        return m_content;
    }

    // False for a content widget that opts out of the header (see
    // DashboardWidget::wantsCellHeader()) — DashboardGrid uses this to
    // allow a much smaller minimum size when resizing, since there's no
    // header to keep legible. True even while the "On hover" card header
    // style hides the strip in Run mode.
    bool hasHeader() const {
        return headerStripHeight() > 0;
    }

    void setTitle(const QString& title);
    void setEditMode(bool enabled);
    void setSelected(bool selected);
    bool isSelected() const {
        return m_selected;
    }
    // False while part of a multi-selection/group (see
    // DashboardGrid::updateResizableFlags()) -- hides the resize grip and
    // disables handleAt() so a multi/group drag can only move, not resize.
    // Default true (a lone selected cell is always resizable).
    void setResizable(bool resizable);
    // Drives the header's connection-status dot (only drawn when m_content
    // offers HeaderControl::ConnectionDot, see DashboardWidget::headerControls()) --
    // set from whichever device this cell's own widget is currently
    // configured for (config()["deviceId"]), propagated through
    // DashboardGrid::setDeviceConnected(deviceId, connected).
    void setConnected(bool connected);
    // Swaps the selection border to the palette's danger color instead of
    // accent — live feedback while a drag/resize candidate would be
    // rejected on release (see DashboardGrid::isPlacementValid()), so the
    // cell no longer just silently snaps back with no warning mid-drag.
    void setDragInvalid(bool invalid);

signals:
    void dragStarted(const QString& itemId, const QPoint& globalPos);
    void dragMoved(const QString& itemId, const QPoint& globalPos);
    void dragFinished(const QString& itemId, const QPoint& globalPos);
    void resizeStarted(const QString& itemId, const QPoint& globalPos,
                       DashboardCell::ResizeHandle handle);
    void resizeMoved(const QString& itemId, const QPoint& globalPos);
    void resizeFinished(const QString& itemId, const QPoint& globalPos);
    // modifiers is checked for Qt::ControlModifier by DashboardGrid to
    // decide between a plain Replace-selection and a Ctrl-click toggle.
    void selectRequested(const QString& itemId, Qt::KeyboardModifiers modifiers);
    // The header's clear button was clicked, after the content widget has
    // already cleared itself.
    void chartDataCleared(DashboardWidget* content);
    // The gear menu changed the content widget's view state, after the
    // widget has already applied it (so a cell outside any grid, like the
    // Debug charts window, still works) -- DashboardGrid persists `view`
    // into the item's config.
    void viewConfigChanged(const QString& itemId, const QJsonObject& view);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class DragMode { None, Moving, Resizing };

    // The title strip's height (0 if m_content opts out via
    // DashboardWidget::wantsCellHeader()), from the current density.
    int headerStripHeight() const;
    // Space the strip takes above the content: headerStripHeight(), except
    // 0 in Run mode under the "On hover" card header style, where the strip
    // floats over the content instead (m_hoverHeader).
    int headerHeight() const;
    // Where the strip is drawn, whether it reserves space or floats.
    QRect headerRect() const;
    // Draws the strip -- type glyph, connection dot, title, buttons -- in
    // the current card header style. `floating` is the hover strip, drawn
    // over the content with a near-opaque fill.
    void paintHeader(QPainter& painter, bool floating) const;
    // Runs the header button under `pos`, if any. Run mode only.
    bool handleHeaderPress(const QPoint& pos);
    void startSelectionAnimation(QAbstractAnimation::Direction direction);
    QRect gripRect() const;
    // Where one of the content widget's header buttons sits: packed from the
    // header's right edge in the order Settings, Clear, Pause, skipping the
    // ones it doesn't offer (DashboardWidget::headerControls()). Empty when
    // it isn't offered, or has no button (ConnectionDot).
    QRect headerButtonRect(DashboardWidget::HeaderControl control) const;
    // Opens the header gear's popup menu, built from the content widget's
    // own DashboardWidget::viewOptions(), anchored below the gear button.
    void showSettingsMenu();
    ResizeHandle handleAt(const QPoint& pos) const;
    Qt::CursorShape cursorForHandle(ResizeHandle handle) const;
    void layoutChildren();
    void updateContentMask();
    void updateCursor();

    QString m_itemId;
    QString m_typeId;
    QString m_title;
    DashboardWidget* m_content = nullptr;
    QWidget* m_borderOverlay = nullptr;
    QWidget* m_hoverHeader = nullptr;  // see headerHeight()
    bool m_editMode = false;
    bool m_selected = false;
    bool m_resizable = true;
    bool m_dragInvalid = false;
    bool m_connected = false;
    DragMode m_dragMode = DragMode::None;
    ResizeHandle m_resizeHandle = ResizeHandle::None;

    // Animates the border between its unselected and selected color instead
    // of snapping (see "Motion" in docs/VISUAL_IDENTITY.md) — value is the
    // 0..1 blend factor toward the selected color.
    QVariantAnimation m_selectionAnim;
};

}  // namespace traceview
