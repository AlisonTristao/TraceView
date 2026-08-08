#include "dashboardgrid.h"

#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>

#include "dashboardcell.h"
#include "traceview/thememanager.h"
#include "widgetregistry.h"

namespace traceview {

namespace {
// Fixed and deliberately small so the grid reads as fine-grained positioning
// resolution — not user-configurable: every item position/size is an exact
// multiple of this in pixels, so there is no rounding math anywhere in this
// file and items are always pixel-exact on the grid lines.
constexpr int kCellSize = 16;
constexpr int kMargin = 8;
// A widget below this many cells in either dimension is too small to be
// usable (header alone is 24px), regardless of how small kCellSize is.
constexpr int kMinSpanCells = 5;
constexpr int kDefaultItemColumnSpan = 16;
constexpr int kDefaultItemRowSpan = 12;
} // namespace

DashboardGrid::DashboardGrid(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

void DashboardGrid::setEditMode(bool enabled) {
    if (m_editMode == enabled) {
        return;
    }
    m_editMode = enabled;
    if (!enabled) {
        selectItem(QString());
    }
    const QList<DashboardCell*> cells = m_cells.values();
    for (DashboardCell* cell : cells) {
        cell->setEditMode(enabled);
    }
    update();
}

void DashboardGrid::selectItem(const QString& itemId) {
    if (m_selectedItemId == itemId) {
        return;
    }
    if (DashboardCell* previous = m_cells.value(m_selectedItemId)) {
        previous->setSelected(false);
    }
    m_selectedItemId = itemId;
    if (DashboardCell* next = m_cells.value(m_selectedItemId)) {
        next->setSelected(true);
    }
    emit selectionChanged(m_selectedItemId);
}

QString DashboardGrid::selectedItemTypeId() const {
    if (const DashboardItem* item = itemById(m_selectedItemId)) {
        return item->typeId;
    }
    return QString();
}

void DashboardGrid::addItem(const QString& typeId) {
    const int columnSpan = qMin(kDefaultItemColumnSpan, columns());
    const int rowSpan = qMin(kDefaultItemRowSpan, rows());

    int column = 0;
    int row = 0;
    if (!findFreeSlot(columnSpan, rowSpan, &column, &row)) {
        column = 0;
        row = 0;
    }

    pushUndoSnapshot();

    DashboardItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.typeId = typeId;
    item.column = column;
    item.row = row;
    item.columnSpan = columnSpan;
    item.rowSpan = rowSpan;

    m_items.append(item);
    createCell(item);
    updateGeometry();
}

void DashboardGrid::removeSelected() {
    if (m_selectedItemId.isEmpty()) {
        return;
    }
    pushUndoSnapshot();
    const QString id = m_selectedItemId;
    m_selectedItemId.clear();
    removeItem(id);
    emit selectionChanged(QString());
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

void DashboardGrid::changeSelectedType(const QString& newTypeId) {
    if (m_selectedItemId.isEmpty()) {
        return;
    }
    DashboardItem* item = itemById(m_selectedItemId);
    if (!item || item->typeId == newTypeId || WidgetRegistry::instance().displayName(newTypeId).isEmpty()) {
        return;
    }

    pushUndoSnapshot();

    item->typeId = newTypeId;
    const QString id = m_selectedItemId;
    if (DashboardCell* oldCell = m_cells.take(id)) {
        oldCell->deleteLater();
    }
    if (DashboardCell* newCell = createCell(*item)) {
        newCell->setSelected(true);
    }
}

void DashboardGrid::undo() {
    if (m_undoStack.isEmpty()) {
        return;
    }
    m_redoStack.append(toJson());
    const QJsonObject previous = m_undoStack.takeLast();
    fromJson(previous);
    emit historyChanged();
}

void DashboardGrid::redo() {
    if (m_redoStack.isEmpty()) {
        return;
    }
    m_undoStack.append(toJson());
    const QJsonObject next = m_redoStack.takeLast();
    fromJson(next);
    emit historyChanged();
}

void DashboardGrid::pushUndoSnapshot() {
    m_undoStack.append(toJson());
    m_redoStack.clear();
    emit historyChanged();
}

QJsonObject DashboardGrid::toJson() const {
    QJsonObject object;

    QJsonArray items;
    for (const DashboardItem& item : m_items) {
        items.append(dashboardItemToJson(item));
    }
    object["items"] = items;
    return object;
}

void DashboardGrid::fromJson(const QJsonObject& object) {
    clearItems();

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
    // A sane floor so the grid never collapses to zero.
    return QSize(320, 240);
}

void DashboardGrid::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayout();
}

void DashboardGrid::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    // Always-visible border delimiting the canvas — where widgets can be
    // placed — separate from the grid lines below (edit mode only).
    const QRect area = usableRect();
    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(area.adjusted(-1, -1, 0, 0));

    if (!m_editMode) {
        return;
    }

    const int cols = columns();
    const int rws = rows();
    const int right = area.left() + cols * kCellSize;
    const int bottom = area.top() + rws * kCellSize;

    painter.setPen(QPen(palette.borderStrong, 1));
    for (int c = 0; c <= cols; ++c) {
        const int x = area.left() + c * kCellSize;
        painter.drawLine(x, area.top(), x, bottom);
    }
    for (int r = 0; r <= rws; ++r) {
        const int y = area.top() + r * kCellSize;
        painter.drawLine(area.left(), y, right, y);
    }
}

void DashboardGrid::mousePressEvent(QMouseEvent* event) {
    // Reaches us only for clicks that missed every cell (Qt delivers to the
    // topmost child widget directly otherwise) — i.e. empty grid space.
    if (m_editMode && event->button() == Qt::LeftButton) {
        selectItem(QString());
    }
    QWidget::mousePressEvent(event);
}

QRect DashboardGrid::usableRect() const {
    return rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
}

int DashboardGrid::columns() const {
    return qMax(1, usableRect().width() / kCellSize);
}

int DashboardGrid::rows() const {
    return qMax(1, usableRect().height() / kCellSize);
}

QRect DashboardGrid::cellRect(const DashboardItem& item) const {
    const QRect area = usableRect();
    const int left = area.left() + item.column * kCellSize;
    const int top = area.top() + item.row * kCellSize;
    return QRect(left, top, item.columnSpan * kCellSize, item.rowSpan * kCellSize);
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
    m_selectedItemId.clear();
}

bool DashboardGrid::findFreeSlot(int columnSpan, int rowSpan, int* outColumn, int* outRow) const {
    const int cols = columns();
    const int rws = rows();
    for (int r = 0; r + rowSpan <= rws; ++r) {
        for (int c = 0; c + columnSpan <= cols; ++c) {
            DashboardItem probe;
            probe.column = c;
            probe.row = r;
            probe.columnSpan = columnSpan;
            probe.rowSpan = rowSpan;
            if (isPlacementValid(probe, QString())) {
                *outColumn = c;
                *outRow = r;
                return true;
            }
        }
    }
    return false;
}

bool DashboardGrid::isPlacementValid(const DashboardItem& candidate, const QString& excludeId) const {
    if (candidate.column < 0 || candidate.row < 0) {
        return false;
    }
    if (candidate.column + candidate.columnSpan > columns() || candidate.row + candidate.rowSpan > rows()) {
        return false;
    }

    for (const DashboardItem& other : m_items) {
        if (other.id == excludeId) {
            continue;
        }
        const bool overlapsColumns =
            candidate.column < other.column + other.columnSpan && other.column < candidate.column + candidate.columnSpan;
        const bool overlapsRows = candidate.row < other.row + other.rowSpan && other.row < candidate.row + candidate.rowSpan;
        if (overlapsColumns && overlapsRows) {
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
    cell->setGeometry(cellRect(item));
    cell->show();

    connect(cell, &DashboardCell::dragStarted, this, &DashboardGrid::handleDragStarted);
    connect(cell, &DashboardCell::dragMoved, this, &DashboardGrid::handleDragMoved);
    connect(cell, &DashboardCell::dragFinished, this, &DashboardGrid::handleDragFinished);
    connect(cell, &DashboardCell::resizeStarted, this, &DashboardGrid::handleResizeStarted);
    connect(cell, &DashboardCell::resizeMoved, this, &DashboardGrid::handleResizeMoved);
    connect(cell, &DashboardCell::resizeFinished, this, &DashboardGrid::handleResizeFinished);
    connect(cell, &DashboardCell::selectRequested, this, &DashboardGrid::handleSelectRequested);

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
    const int deltaColumn = qRound(deltaPx.x() / double(kCellSize));
    const int deltaRow = qRound(deltaPx.y() / double(kCellSize));

    DashboardItem candidate = m_drag->original;
    candidate.column = qBound(0, m_drag->original.column + deltaColumn, qMax(0, columns() - candidate.columnSpan));
    candidate.row = qBound(0, m_drag->original.row + deltaRow, qMax(0, rows() - candidate.rowSpan));
    m_drag->candidate = candidate;

    if (DashboardCell* draggedCell = m_cells.value(itemId)) {
        draggedCell->setGeometry(cellRect(candidate));
    }
}

void DashboardGrid::handleDragFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->itemId != itemId) {
        return;
    }
    if (isPlacementValid(m_drag->candidate, itemId)) {
        if (DashboardItem* item = itemById(itemId)) {
            if (item->column != m_drag->candidate.column || item->row != m_drag->candidate.row) {
                pushUndoSnapshot();
                item->column = m_drag->candidate.column;
                item->row = m_drag->candidate.row;
            }
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
    const int deltaColumn = qRound(deltaPx.x() / double(kCellSize));
    const int deltaRow = qRound(deltaPx.y() / double(kCellSize));

    DashboardItem candidate = m_drag->original;
    candidate.columnSpan = qBound(kMinSpanCells, m_drag->original.columnSpan + deltaColumn,
                                   qMax(kMinSpanCells, columns() - candidate.column));
    candidate.rowSpan =
        qBound(kMinSpanCells, m_drag->original.rowSpan + deltaRow, qMax(kMinSpanCells, rows() - candidate.row));
    m_drag->candidate = candidate;

    if (DashboardCell* resizedCell = m_cells.value(itemId)) {
        resizedCell->setGeometry(cellRect(candidate));
    }
}

void DashboardGrid::handleResizeFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->itemId != itemId) {
        return;
    }
    if (isPlacementValid(m_drag->candidate, itemId)) {
        if (DashboardItem* item = itemById(itemId)) {
            if (item->columnSpan != m_drag->candidate.columnSpan || item->rowSpan != m_drag->candidate.rowSpan) {
                pushUndoSnapshot();
                item->columnSpan = m_drag->candidate.columnSpan;
                item->rowSpan = m_drag->candidate.rowSpan;
            }
        }
    }
    m_drag.reset();
    relayoutItem(itemId);
}

void DashboardGrid::handleSelectRequested(const QString& itemId) {
    selectItem(itemId);
}

} // namespace traceview
