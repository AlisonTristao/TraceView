#include "dashboardcell.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

#include "dashboardwidget.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kHeaderHeight = 24;
constexpr int kGripSize = 14;
constexpr int kRemoveButtonSize = 18;
} // namespace

DashboardCell::DashboardCell(const QString& itemId, const QString& title, DashboardWidget* content,
                              QWidget* parent)
    : QWidget(parent), m_itemId(itemId), m_title(title), m_content(content) {
    m_content->setParent(this);

    m_removeButton = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), this); // "✕"
    m_removeButton->setFixedSize(kRemoveButtonSize, kRemoveButtonSize);
    m_removeButton->setCursor(Qt::ArrowCursor);
    m_removeButton->setVisible(false);
    connect(m_removeButton, &QPushButton::clicked, this, [this]() { emit removeRequested(m_itemId); });

    layoutChildren();
}

void DashboardCell::setEditMode(bool enabled) {
    if (m_editMode == enabled) {
        return;
    }
    m_editMode = enabled;
    m_removeButton->setVisible(enabled);
    layoutChildren();
    update();
}

QRect DashboardCell::headerRect() const {
    return QRect(0, 0, width(), kHeaderHeight);
}

QRect DashboardCell::gripRect() const {
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

void DashboardCell::layoutChildren() {
    m_removeButton->move(width() - kRemoveButtonSize - 2, 2);
    if (m_editMode) {
        m_content->setGeometry(0, kHeaderHeight, width(), height() - kHeaderHeight);
    } else {
        m_content->setGeometry(rect());
    }
}

void DashboardCell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutChildren();
}

void DashboardCell::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    painter.setPen(QPen(palette.borderStrong, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if (!m_editMode) {
        return;
    }

    painter.fillRect(headerRect(), palette.surfaceAlt);
    painter.setPen(palette.textPrimary);
    painter.drawText(headerRect().adjusted(6, 0, -kRemoveButtonSize - 6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      m_title);

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

} // namespace traceview
