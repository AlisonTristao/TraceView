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
    DashboardCell(const QString& itemId, const QString& title, DashboardWidget* content,
                  QWidget* parent = nullptr);

    QString itemId() const { return m_itemId; }

    void setEditMode(bool enabled);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    void dragStarted(const QString& itemId, const QPoint& globalPos);
    void dragMoved(const QString& itemId, const QPoint& globalPos);
    void dragFinished(const QString& itemId, const QPoint& globalPos);
    void resizeStarted(const QString& itemId, const QPoint& globalPos);
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

    QRect headerRect() const;
    QRect gripRect() const;
    void layoutChildren();
    void updateCursor();

    QString m_itemId;
    QString m_title;
    DashboardWidget* m_content = nullptr;
    bool m_editMode = false;
    bool m_selected = false;
    DragMode m_dragMode = DragMode::None;
};

} // namespace traceview
