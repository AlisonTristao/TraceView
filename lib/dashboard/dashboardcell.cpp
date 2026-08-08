#include "dashboardcell.h"

#include <QMouseEvent>
#include <QPainter>

#include "dashboardwidget.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kHeaderHeight = 24;
constexpr int kGripSize = 14;
} // namespace

DashboardCell::DashboardCell(const QString& itemId, const QString& title, DashboardWidget* content,
                              QWidget* parent)
    : QWidget(parent), m_itemId(itemId), m_title(title), m_content(content) {
    m_content->setParent(this);

    setMouseTracking(true);
    layoutChildren();
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
        m_removeMode = false;
        m_typeEditMode = false;
    }
    updateCursor();
    layoutChildren();
    update();
}

void DashboardCell::setRemoveMode(bool enabled) {
    if (m_removeMode == enabled) {
        return;
    }
    m_removeMode = enabled;
    updateCursor();
    update();
}

void DashboardCell::setTypeEditMode(bool enabled) {
    if (m_typeEditMode == enabled) {
        return;
    }
    m_typeEditMode = enabled;
    updateCursor();
    update();
}

QRect DashboardCell::headerRect() const {
    return QRect(0, 0, width(), kHeaderHeight);
}

QRect DashboardCell::gripRect() const {
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

void DashboardCell::layoutChildren() {
    if (m_editMode) {
        m_content->setGeometry(0, kHeaderHeight, width(), height() - kHeaderHeight);
    } else {
        m_content->setGeometry(rect());
    }
}

void DashboardCell::updateCursor() {
    if (m_removeMode || m_typeEditMode) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
}

void DashboardCell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutChildren();
}

void DashboardCell::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    QColor borderColor = palette.borderStrong;
    int borderWidth = 1;
    if (m_removeMode) {
        borderColor = palette.danger;
        borderWidth = 2;
    } else if (m_typeEditMode) {
        borderColor = palette.accent;
        borderWidth = 2;
    }
    painter.setPen(QPen(borderColor, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (!m_editMode) {
        return;
    }

    painter.fillRect(headerRect(), palette.surfaceAlt);
    painter.setPen(palette.textPrimary);
    painter.drawText(headerRect().adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, m_title);

    if (m_removeMode || m_typeEditMode) {
        // Resize doesn't apply while in a click-to-act mode; only the grip
        // is hidden, the header stays visible for identification.
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

    if (m_removeMode) {
        emit removeRequested(m_itemId);
        event->accept();
        return;
    }
    if (m_typeEditMode) {
        emit typeEditRequested(m_itemId);
        event->accept();
        return;
    }

    const QPoint pos = event->position().toPoint();
    if (gripRect().contains(pos)) {
        m_dragMode = DragMode::Resizing;
        emit resizeStarted(m_itemId, event->globalPosition().toPoint());
    } else if (headerRect().contains(pos)) {
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

    if (m_editMode && !m_removeMode && !m_typeEditMode) {
        const QPoint pos = event->position().toPoint();
        if (gripRect().contains(pos)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (headerRect().contains(pos)) {
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
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void DashboardCell::leaveEvent(QEvent* event) {
    if (!m_removeMode && !m_typeEditMode) {
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

} // namespace traceview
