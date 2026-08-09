#include "dashboardcell.h"

#include <QMouseEvent>
#include <QPainter>

#include "dashboardwidget.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kHeaderHeight = 24;
constexpr int kGripSize = 14;   // hit/visual size of the 4 corner handles
constexpr int kEdgeMargin = 6;  // hit thickness of the 4 edge handles
} // namespace

DashboardCell::DashboardCell(const QString& itemId, const QString& title, DashboardWidget* content,
                              QWidget* parent)
    : QWidget(parent), m_itemId(itemId), m_title(title), m_content(content) {
    m_content->setParent(this);

    setMouseTracking(true);
    layoutChildren();
}

void DashboardCell::setTitle(const QString& title) {
    if (m_title == title) {
        return;
    }
    m_title = title;
    update();
}

void DashboardCell::setEditMode(bool enabled) {
    if (m_editMode == enabled) {
        return;
    }
    m_editMode = enabled;
    // The content widget spans the full cell below the header, which
    // includes the resize grip's corner. Without this, clicks/hover on the
    // grip land on the content widget instead of us, so drag/resize misses
    // the mouse press (and hover cursor feedback) entirely.
    m_content->setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
    if (!enabled) {
        m_selected = false;
    }
    updateCursor();
    layoutChildren();
    update();
}

void DashboardCell::setSelected(bool selected) {
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    updateCursor();
    update();
}

int DashboardCell::headerHeight() const {
    return m_content->wantsCellHeader() ? kHeaderHeight : 0;
}

QRect DashboardCell::headerRect() const {
    return QRect(0, 0, width(), headerHeight());
}

QRect DashboardCell::gripRect() const {
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

DashboardCell::ResizeHandle DashboardCell::handleAt(const QPoint& pos) const {
    const bool nearLeft = pos.x() <= kGripSize;
    const bool nearRight = pos.x() >= width() - kGripSize;
    const bool nearTop = pos.y() <= kGripSize;
    const bool nearBottom = pos.y() >= height() - kGripSize;

    // Corners (bigger squares) take priority over the thinner edge bands.
    if (nearTop && nearLeft) return ResizeHandle::TopLeft;
    if (nearTop && nearRight) return ResizeHandle::TopRight;
    if (nearBottom && nearLeft) return ResizeHandle::BottomLeft;
    if (nearBottom && nearRight) return ResizeHandle::BottomRight;

    if (pos.y() <= kEdgeMargin) return ResizeHandle::Top;
    if (pos.y() >= height() - kEdgeMargin) return ResizeHandle::Bottom;
    if (pos.x() <= kEdgeMargin) return ResizeHandle::Left;
    if (pos.x() >= width() - kEdgeMargin) return ResizeHandle::Right;

    return ResizeHandle::None;
}

Qt::CursorShape DashboardCell::cursorForHandle(ResizeHandle handle) const {
    switch (handle) {
        case ResizeHandle::TopLeft:
        case ResizeHandle::BottomRight:
            return Qt::SizeFDiagCursor;
        case ResizeHandle::TopRight:
        case ResizeHandle::BottomLeft:
            return Qt::SizeBDiagCursor;
        case ResizeHandle::Top:
        case ResizeHandle::Bottom:
            return Qt::SizeVerCursor;
        case ResizeHandle::Left:
        case ResizeHandle::Right:
            return Qt::SizeHorCursor;
        case ResizeHandle::None:
            break;
    }
    return Qt::ArrowCursor;
}

void DashboardCell::layoutChildren() {
    if (m_editMode) {
        const int headerH = headerHeight();
        m_content->setGeometry(0, headerH, width(), height() - headerH);
    } else {
        m_content->setGeometry(rect());
    }
}

void DashboardCell::updateCursor() {
    if (m_editMode && !m_selected) {
        setCursor(Qt::PointingHandCursor); // click to select
    } else {
        unsetCursor(); // selected: hover logic below drives it; not editing: default
    }
}

void DashboardCell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutChildren();
}

void DashboardCell::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    const QColor borderColor = m_selected ? palette.accent : palette.borderStrong;
    const int borderWidth = m_selected ? 2 : 1;
    painter.setPen(QPen(borderColor, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (!m_editMode) {
        return;
    }

    if (headerHeight() > 0) {
        painter.fillRect(headerRect(), m_selected ? palette.accent : palette.surfaceAlt);
        painter.setPen(m_selected ? palette.background : palette.textPrimary);
        painter.drawText(headerRect().adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, m_title);
    }

    if (!m_selected) {
        // Unselected cells are identifiable but not interactive — no grip.
        return;
    }

    painter.setPen(QPen(palette.textSecondary, 1));
    const QRect grip = gripRect();
    for (int i = 1; i <= 3; ++i) {
        const int offset = i * 4;
        painter.drawLine(grip.right() - offset, grip.bottom(), grip.right(), grip.bottom() - offset);
    }
}

void DashboardCell::mousePressEvent(QMouseEvent* event) {
    if (!m_editMode || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (!m_selected) {
        emit selectRequested(m_itemId);
        event->accept();
        return;
    }

    const QPoint pos = event->position().toPoint();
    const ResizeHandle handle = handleAt(pos);
    // A header-less cell (see DashboardWidget::wantsCellHeader) has no
    // dedicated drag handle to grab, so the whole selected body doubles as
    // one instead — clicking anywhere that isn't a resize handle starts a
    // move, same as clicking the header does for other kinds.
    if (handle != ResizeHandle::None) {
        m_dragMode = DragMode::Resizing;
        m_resizeHandle = handle;
        emit resizeStarted(m_itemId, event->globalPosition().toPoint(), handle);
    } else if (headerHeight() == 0 || headerRect().contains(pos)) {
        m_dragMode = DragMode::Moving;
        emit dragStarted(m_itemId, event->globalPosition().toPoint());
    } else {
        QWidget::mousePressEvent(event);
        return;
    }
    event->accept();
}

void DashboardCell::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragMode == DragMode::Moving) {
        emit dragMoved(m_itemId, event->globalPosition().toPoint());
        event->accept();
        return;
    }
    if (m_dragMode == DragMode::Resizing) {
        emit resizeMoved(m_itemId, event->globalPosition().toPoint());
        event->accept();
        return;
    }

    if (m_editMode && m_selected) {
        const QPoint pos = event->position().toPoint();
        const ResizeHandle handle = handleAt(pos);
        if (handle != ResizeHandle::None) {
            setCursor(cursorForHandle(handle));
        } else if (headerHeight() == 0 || headerRect().contains(pos)) {
            setCursor(Qt::SizeAllCursor);
        } else {
            unsetCursor();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void DashboardCell::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragMode == DragMode::Moving) {
        emit dragFinished(m_itemId, event->globalPosition().toPoint());
        m_dragMode = DragMode::None;
        event->accept();
        return;
    }
    if (m_dragMode == DragMode::Resizing) {
        emit resizeFinished(m_itemId, event->globalPosition().toPoint());
        m_dragMode = DragMode::None;
        m_resizeHandle = ResizeHandle::None;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void DashboardCell::leaveEvent(QEvent* event) {
    if (m_selected) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

} // namespace traceview
