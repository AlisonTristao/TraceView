#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

namespace traceview {

class DashboardWidget;

// Chrome wrapped around one DashboardWidget on the grid. In edit mode,
// clicking an unselected cell just selects it (only one selected at a
// time, grid-wide — DashboardGrid enforces that); the selected cell shows
// a header strip (drag handle) and a resize grip, unselected cells stay
// flat. This widget does no grid math itself — it only reports raw cursor
// positions via signals; DashboardGrid owns cell-size/collision logic and
// decides where the cell actually ends up.
class DashboardCell : public QWidget {
    Q_OBJECT

public:
    // Which part of the border was grabbed. The 4 edges resize a single
    // dimension (anchoring the opposite edge in place); the 4 corners
    // resize both dimensions at once.
    enum class ResizeHandle { None, Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight };

    DashboardCell(const QString& itemId, const QString& title, DashboardWidget* content,
                  QWidget* parent = nullptr);

    QString itemId() const { return m_itemId; }
    DashboardWidget* content() const { return m_content; }

    // False for a content widget that opts out of the header (see
    // DashboardWidget::wantsCellHeader()) — DashboardGrid uses this to
    // allow a much smaller minimum size when resizing, since there's no
    // 24px header to keep legible.
    bool hasHeader() const { return headerHeight() > 0; }

    void setTitle(const QString& title);
    void setEditMode(bool enabled);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    void dragStarted(const QString& itemId, const QPoint& globalPos);
    void dragMoved(const QString& itemId, const QPoint& globalPos);
    void dragFinished(const QString& itemId, const QPoint& globalPos);
    void resizeStarted(const QString& itemId, const QPoint& globalPos, DashboardCell::ResizeHandle handle);
    void resizeMoved(const QString& itemId, const QPoint& globalPos);
    void resizeFinished(const QString& itemId, const QPoint& globalPos);
    void selectRequested(const QString& itemId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class DragMode { None, Moving, Resizing };

    // 0 if m_content opts out via DashboardWidget::wantsCellHeader(),
    // otherwise the fixed header strip height.
    int headerHeight() const;
    QRect headerRect() const;
    QRect gripRect() const;
    ResizeHandle handleAt(const QPoint& pos) const;
    Qt::CursorShape cursorForHandle(ResizeHandle handle) const;
    void layoutChildren();
    void updateCursor();

    QString m_itemId;
    QString m_title;
    DashboardWidget* m_content = nullptr;
    bool m_editMode = false;
    bool m_selected = false;
    DragMode m_dragMode = DragMode::None;
    ResizeHandle m_resizeHandle = ResizeHandle::None;
};

} // namespace traceview
