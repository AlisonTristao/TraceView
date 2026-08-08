#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

class QPushButton;

namespace traceview {

class DashboardWidget;

// Chrome wrapped around one DashboardWidget on the grid: a header strip
// (drag handle + remove button) and a resize grip, both shown only in edit
// mode. This widget does no grid math itself — it only reports raw cursor
// positions via signals; DashboardGrid owns cell-size/collision logic and
// decides where the cell actually ends up.
class DashboardCell : public QWidget {
    Q_OBJECT

public:
    DashboardCell(const QString& itemId, const QString& title, DashboardWidget* content,
                  QWidget* parent = nullptr);

    QString itemId() const { return m_itemId; }

    void setEditMode(bool enabled);

signals:
    void dragStarted(const QString& itemId, const QPoint& globalPos);
    void dragMoved(const QString& itemId, const QPoint& globalPos);
    void dragFinished(const QString& itemId, const QPoint& globalPos);
    void resizeStarted(const QString& itemId, const QPoint& globalPos);
    void resizeMoved(const QString& itemId, const QPoint& globalPos);
    void resizeFinished(const QString& itemId, const QPoint& globalPos);
    void removeRequested(const QString& itemId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragMode { None, Moving, Resizing };

    QRect headerRect() const;
    QRect gripRect() const;
    void layoutChildren();

    QString m_itemId;
    QString m_title;
    DashboardWidget* m_content = nullptr;
    QPushButton* m_removeButton = nullptr;
    bool m_editMode = false;
    DragMode m_dragMode = DragMode::None;
};

} // namespace traceview
