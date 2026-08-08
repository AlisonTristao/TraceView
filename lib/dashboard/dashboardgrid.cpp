#include "dashboardgrid.h"

#include <QJsonArray>
#include <QPainter>
#include <QUuid>

#include "dashboardcell.h"
#include "traceview/thememanager.h"
#include "widgetregistry.h"

namespace traceview {

namespace {
constexpr int kDefaultColumns = 12;
constexpr int kDefaultRows = 8;
constexpr int kMargin = 8;
constexpr int kSpacing = 6;
constexpr int kMinCellSize = 32;
constexpr int kDefaultItemRowSpan = 2;
constexpr int kDefaultItemColumnSpan = 3;
} // namespace

DashboardGrid::DashboardGrid(QWidget* parent)
    : QWidget(parent), m_columns(kDefaultColumns), m_rows(kDefaultRows) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

void DashboardGrid::setEditMode(bool enabled) {
    if (m_editMode == enabled) {
        return;
    }
    m_editMode = enabled;
    const QList<DashboardCell*> cells = m_cells.values();
    for (DashboardCell* cell : cells) {
        cell->setEditMode(enabled);
    }
    update();
}

void DashboardGrid::setRemoveMode(bool enabled) {
    if (m_removeMode == enabled) {
        return;
    }
    m_removeMode = enabled;
    const QList<DashboardCell*> cells = m_cells.values();
    for (DashboardCell* cell : cells) {
        cell->setRemoveMode(enabled);
    }
}

void DashboardGrid::setTypeEditMode(bool enabled) {
    if (m_typeEditMode == enabled) {
        return;
    }
    m_typeEditMode = enabled;
    const QList<DashboardCell*> cells = m_cells.values();
    for (DashboardCell* cell : cells) {
        cell->setTypeEditMode(enabled);
    }
}

void DashboardGrid::addItem(const QString& typeId) {
    int row = 0;
    int column = 0;
    if (!findFirstFreeCell(kDefaultItemRowSpan, kDefaultItemColumnSpan, &row, &column)) {
        row = 0;
        column = 0;
    }

    DashboardItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.typeId = typeId;
    item.row = row;
    item.column = column;
    item.rowSpan = kDefaultItemRowSpan;
    item.columnSpan = kDefaultItemColumnSpan;

    m_items.append(item);
    createCell(item);
    updateGeometry();
}

void DashboardGrid::removeItem(const QString& itemId) {
    if (DashboardCell* cell = m_cells.take(itemId)) {
        cell->deleteLater();
    }
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == itemId) {
            m_items.remove(i);
            break;
        }
    }
    updateGeometry();
}

void DashboardGrid::changeItemType(const QString& itemId, const QString& newTypeId) {
    DashboardItem* item = itemById(itemId);
    if (!item || item->typeId == newTypeId || WidgetRegistry::instance().displayName(newTypeId).isEmpty()) {
        return;
    }
    item->typeId = newTypeId;

    if (DashboardCell* oldCell = m_cells.take(itemId)) {
        oldCell->deleteLater();
    }
    createCell(*item);
}

QJsonObject DashboardGrid::toJson() const {
    QJsonObject object;
    object["columns"] = m_columns;
    object["rows"] = m_rows;

    QJsonArray items;
    for (const DashboardItem& item : m_items) {
        items.append(dashboardItemToJson(item));
    }
    object["items"] = items;
    return object;
}

void DashboardGrid::fromJson(const QJsonObject& object) {
    clearItems();

    m_columns = object.value("columns").toInt(kDefaultColumns);
    m_rows = object.value("rows").toInt(kDefaultRows);

    const QJsonArray items = object.value("items").toArray();
    for (const QJsonValue& value : items) {
        bool ok = false;
        DashboardItem item = dashboardItemFromJson(value.toObject(), &ok);
        if (!ok || WidgetRegistry::instance().displayName(item.typeId).isEmpty()) {
            continue;
        }
        m_items.append(item);
        createCell(item);
    }

    updateGeometry();
    relayout();
}

QSize DashboardGrid::sizeHint() const {
    return contentSize();
}

QSize DashboardGrid::minimumSizeHint() const {
    return contentSize();
}

QSize DashboardGrid::contentSize() const {
    const int width = 2 * kMargin + m_columns * kMinCellSize + qMax(0, m_columns - 1) * kSpacing;
    const int height = 2 * kMargin + m_rows * kMinCellSize + qMax(0, m_rows - 1) * kSpacing;
    return QSize(width, height);
}

void DashboardGrid::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayout();
}

void DashboardGrid::paintEvent(QPaintEvent*) {
    if (!m_editMode) {
        return;
    }

    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    painter.setPen(QPen(palette.border, 1));

    // Continuous grid lines (not a rect per cell) so adjacent cells share
    // one line instead of drawing overlapping/rounded-off corners.
    const qreal colStep = columnStep();
    const qreal rowStepPx = rowStep();
    const int right = kMargin + qRound(m_columns * colStep) - kSpacing;
    const int bottom = kMargin + qRound(m_rows * rowStepPx) - kSpacing;

    for (int c = 0; c <= m_columns; ++c) {
        const int x = kMargin + qRound(c * colStep);
        painter.drawLine(x, kMargin, x, bottom);
    }
    for (int r = 0; r <= m_rows; ++r) {
        const int y = kMargin + qRound(r * rowStepPx);
        painter.drawLine(kMargin, y, right, y);
    }
}

qreal DashboardGrid::cellWidth() const {
    const qreal usable = width() - 2.0 * kMargin - qMax(0, m_columns - 1) * kSpacing;
    return qMax(1.0, usable / m_columns);
}

qreal DashboardGrid::cellHeight() const {
    const qreal usable = height() - 2.0 * kMargin - qMax(0, m_rows - 1) * kSpacing;
    return qMax(1.0, usable / m_rows);
}

qreal DashboardGrid::columnStep() const {
    return cellWidth() + kSpacing;
}

qreal DashboardGrid::rowStep() const {
    return cellHeight() + kSpacing;
}

QRect DashboardGrid::cellRect(const DashboardItem& item) const {
    const qreal cw = cellWidth();
    const qreal ch = cellHeight();
    const int x = kMargin + qRound(item.column * columnStep());
    const int y = kMargin + qRound(item.row * rowStep());
    const int w = qRound(item.columnSpan * cw + qMax(0, item.columnSpan - 1) * kSpacing);
    const int h = qRound(item.rowSpan * ch + qMax(0, item.rowSpan - 1) * kSpacing);
    return QRect(x, y, w, h);
}

void DashboardGrid::relayout() {
    for (const DashboardItem& item : m_items) {
        if (DashboardCell* cell = m_cells.value(item.id)) {
            cell->setGeometry(cellRect(item));
        }
    }
}

void DashboardGrid::relayoutItem(const QString& itemId) {
    if (const DashboardItem* item = itemById(itemId)) {
        if (DashboardCell* cell = m_cells.value(itemId)) {
            cell->setGeometry(cellRect(*item));
        }
    }
}

void DashboardGrid::clearItems() {
    qDeleteAll(m_cells);
    m_cells.clear();
    m_items.clear();
    m_drag.reset();
}

bool DashboardGrid::findFirstFreeCell(int rowSpan, int columnSpan, int* outRow, int* outColumn) const {
    for (int r = 0; r + rowSpan <= m_rows; ++r) {
        for (int c = 0; c + columnSpan <= m_columns; ++c) {
            DashboardItem probe;
            probe.row = r;
            probe.column = c;
            probe.rowSpan = rowSpan;
            probe.columnSpan = columnSpan;
            if (isPlacementValid(probe, QString())) {
                *outRow = r;
                *outColumn = c;
                return true;
            }
        }
    }
    return false;
}

bool DashboardGrid::isPlacementValid(const DashboardItem& candidate, const QString& excludeId) const {
    if (candidate.row < 0 || candidate.column < 0) {
        return false;
    }
    if (candidate.row + candidate.rowSpan > m_rows || candidate.column + candidate.columnSpan > m_columns) {
        return false;
    }

    for (const DashboardItem& other : m_items) {
        if (other.id == excludeId) {
            continue;
        }
        const bool overlapsRows = candidate.row < other.row + other.rowSpan && other.row < candidate.row + candidate.rowSpan;
        const bool overlapsColumns =
            candidate.column < other.column + other.columnSpan && other.column < candidate.column + candidate.columnSpan;
        if (overlapsRows && overlapsColumns) {
            return false;
        }
    }
    return true;
}

DashboardItem* DashboardGrid::itemById(const QString& itemId) {
    for (DashboardItem& item : m_items) {
        if (item.id == itemId) {
            return &item;
        }
    }
    return nullptr;
}

const DashboardItem* DashboardGrid::itemById(const QString& itemId) const {
    for (const DashboardItem& item : m_items) {
        if (item.id == itemId) {
            return &item;
        }
    }
    return nullptr;
}

DashboardCell* DashboardGrid::createCell(const DashboardItem& item) {
    DashboardWidget* content = WidgetRegistry::instance().create(item.typeId, nullptr);
    if (!content) {
        return nullptr;
    }

    const QString title = WidgetRegistry::instance().displayName(item.typeId);
    auto* cell = new DashboardCell(item.id, title, content, this);
    cell->setEditMode(m_editMode);
    cell->setRemoveMode(m_removeMode);
    cell->setTypeEditMode(m_typeEditMode);
    cell->setGeometry(cellRect(item));
    cell->show();

    connect(cell, &DashboardCell::dragStarted, this, &DashboardGrid::handleDragStarted);
    connect(cell, &DashboardCell::dragMoved, this, &DashboardGrid::handleDragMoved);
    connect(cell, &DashboardCell::dragFinished, this, &DashboardGrid::handleDragFinished);
    connect(cell, &DashboardCell::resizeStarted, this, &DashboardGrid::handleResizeStarted);
    connect(cell, &DashboardCell::resizeMoved, this, &DashboardGrid::handleResizeMoved);
    connect(cell, &DashboardCell::resizeFinished, this, &DashboardGrid::handleResizeFinished);
    connect(cell, &DashboardCell::removeRequested, this, &DashboardGrid::handleRemoveRequested);
    connect(cell, &DashboardCell::typeEditRequested, this, &DashboardGrid::handleTypeEditRequested);

    m_cells.insert(item.id, cell);
    return cell;
}

void DashboardGrid::handleDragStarted(const QString& itemId, const QPoint& globalPos) {
    const DashboardItem* item = itemById(itemId);
    if (!item) {
        return;
    }
    m_drag = DragOp{itemId, globalPos, *item, *item, false};
    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->raise();
    }
}

void DashboardGrid::handleDragMoved(const QString& itemId, const QPoint& globalPos) {
    if (!m_drag || m_drag->itemId != itemId || m_drag->resizing) {
        return;
    }

    const QPoint deltaPx = globalPos - m_drag->startGlobalPos;
    const int deltaColumn = qRound(deltaPx.x() / columnStep());
    const int deltaRow = qRound(deltaPx.y() / double(rowStep()));

    DashboardItem candidate = m_drag->original;
    candidate.row = qBound(0, m_drag->original.row + deltaRow, m_rows - candidate.rowSpan);
    candidate.column = qBound(0, m_drag->original.column + deltaColumn, m_columns - candidate.columnSpan);
    m_drag->candidate = candidate;

    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->setGeometry(cellRect(candidate));
    }
}

void DashboardGrid::handleDragFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->itemId != itemId) {
        return;
    }
    if (isPlacementValid(m_drag->candidate, itemId)) {
        if (DashboardItem* item = itemById(itemId)) {
            item->row = m_drag->candidate.row;
            item->column = m_drag->candidate.column;
        }
    }
    m_drag.reset();
    relayoutItem(itemId);
}

void DashboardGrid::handleResizeStarted(const QString& itemId, const QPoint& globalPos) {
    const DashboardItem* item = itemById(itemId);
    if (!item) {
        return;
    }
    m_drag = DragOp{itemId, globalPos, *item, *item, true};
    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->raise();
    }
}

void DashboardGrid::handleResizeMoved(const QString& itemId, const QPoint& globalPos) {
    if (!m_drag || m_drag->itemId != itemId || !m_drag->resizing) {
        return;
    }

    const QPoint deltaPx = globalPos - m_drag->startGlobalPos;
    const int deltaColumn = qRound(deltaPx.x() / columnStep());
    const int deltaRow = qRound(deltaPx.y() / double(rowStep()));

    DashboardItem candidate = m_drag->original;
    candidate.columnSpan = qBound(1, m_drag->original.columnSpan + deltaColumn, m_columns - candidate.column);
    candidate.rowSpan = qBound(1, m_drag->original.rowSpan + deltaRow, m_rows - candidate.row);
    m_drag->candidate = candidate;

    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->setGeometry(cellRect(candidate));
    }
}

void DashboardGrid::handleResizeFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->itemId != itemId) {
        return;
    }
    if (isPlacementValid(m_drag->candidate, itemId)) {
        if (DashboardItem* item = itemById(itemId)) {
            item->rowSpan = m_drag->candidate.rowSpan;
            item->columnSpan = m_drag->candidate.columnSpan;
        }
    }
    m_drag.reset();
    relayoutItem(itemId);
}

void DashboardGrid::handleRemoveRequested(const QString& itemId) {
    removeItem(itemId);
}

void DashboardGrid::handleTypeEditRequested(const QString& itemId) {
    if (const DashboardItem* item = itemById(itemId)) {
        emit widgetTypeEditRequested(itemId, item->typeId);
    }
}

} // namespace traceview
