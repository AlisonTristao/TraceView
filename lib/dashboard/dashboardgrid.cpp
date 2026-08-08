#include "dashboardgrid.h"

#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QUuid>

#include "dashboardcell.h"
#include "dashboardcommands.h"
#include "traceview/thememanager.h"
#include "widgetregistry.h"

namespace traceview {

namespace {
constexpr int kMargin = 8;
// Fixed logical division count driving grid-line painting and drag/resize
// snap granularity. Deliberately independent of window size — items are
// stored as fractions of the canvas (see DashboardItem), so this constant
// only shapes interaction feel, never item geometry directly. That's what
// keeps relayout() resize-safe: there is no per-window column/row count to
// go stale and clamp items against.
constexpr int kGridColumns = 48;
constexpr int kGridRows = 32;
// A widget below this fraction in either dimension is too small to be
// usable (header alone is 24px). Mirrors the old 5-cell minimum.
constexpr double kMinItemWidth = 5.0 / kGridColumns;
constexpr double kMinItemHeight = 5.0 / kGridRows;
constexpr double kDefaultItemWidth = 16.0 / kGridColumns;
constexpr double kDefaultItemHeight = 12.0 / kGridRows;
// Tolerance for fraction comparisons (bounds/overlap checks), to absorb
// floating-point rounding from snapping math without treating touching
// edges as overlapping.
constexpr double kEpsilon = 1e-6;
} // namespace

DashboardGrid::DashboardGrid(QWidget* parent) : QWidget(parent), m_undoStack(new QUndoStack(this)) {
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

QString DashboardGrid::selectedItemDisplayName() const {
    if (const DashboardItem* item = itemById(m_selectedItemId)) {
        return displayNameFor(*item);
    }
    return QString();
}

QString DashboardGrid::selectedItemKey() const {
    if (const DashboardItem* item = itemById(m_selectedItemId)) {
        return item->key;
    }
    return QString();
}

void DashboardGrid::addItem(const QString& typeId) {
    const double width = kDefaultItemWidth;
    const double height = kDefaultItemHeight;

    double x = 0.0;
    double y = 0.0;
    if (!findFreeSlot(width, height, &x, &y)) {
        x = 0.0;
        y = 0.0;
    }

    DashboardItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.typeId = typeId;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;

    m_undoStack->push(new AddWidgetCommand(this, item));
    // Selected right away so the properties panel comes up already showing
    // it — type/name/key are picked there now, not in an upfront dialog.
    selectItem(item.id);
}

void DashboardGrid::removeSelected() {
    if (m_selectedItemId.isEmpty()) {
        return;
    }
    const DashboardItem* item = itemById(m_selectedItemId);
    if (!item) {
        return;
    }
    m_undoStack->push(new RemoveWidgetCommand(this, *item));
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
    const DashboardItem* item = itemById(m_selectedItemId);
    if (!item || item->typeId == newTypeId || WidgetRegistry::instance().displayName(newTypeId).isEmpty()) {
        return;
    }

    m_undoStack->push(new ChangeWidgetTypeCommand(this, m_selectedItemId, item->typeId, newTypeId));
}

void DashboardGrid::renameSelected(const QString& newName) {
    if (m_selectedItemId.isEmpty()) {
        return;
    }
    const DashboardItem* item = itemById(m_selectedItemId);
    if (!item || item->name == newName) {
        return;
    }

    m_undoStack->push(new RenameWidgetCommand(this, m_selectedItemId, item->name, newName));
}

bool DashboardGrid::setSelectedKey(const QString& newKey) {
    if (m_selectedItemId.isEmpty()) {
        return false;
    }
    const DashboardItem* item = itemById(m_selectedItemId);
    if (!item) {
        return false;
    }
    if (item->key == newKey) {
        return true;
    }
    if (!isKeyAvailable(newKey, m_selectedItemId)) {
        return false;
    }

    m_undoStack->push(new SetItemKeyCommand(this, m_selectedItemId, item->key, newKey));
    return true;
}

void DashboardGrid::applyInsertItem(const DashboardItem& item) {
    m_items.append(item);
    createCell(item);
    updateGeometry();
}

void DashboardGrid::applyRemoveItemById(const QString& itemId) {
    if (m_selectedItemId == itemId) {
        selectItem(QString());
    }
    removeItem(itemId);
}

void DashboardGrid::applyMove(const QString& itemId, const QPointF& position) {
    if (DashboardItem* item = itemById(itemId)) {
        item->x = position.x();
        item->y = position.y();
    }
    relayoutItem(itemId);
}

void DashboardGrid::applyResize(const QString& itemId, const QRectF& geometry) {
    if (DashboardItem* item = itemById(itemId)) {
        item->x = geometry.x();
        item->y = geometry.y();
        item->width = geometry.width();
        item->height = geometry.height();
    }
    relayoutItem(itemId);
}

void DashboardGrid::applyTypeChange(const QString& itemId, const QString& typeId) {
    DashboardItem* item = itemById(itemId);
    if (!item) {
        return;
    }
    item->typeId = typeId;
    if (DashboardCell* oldCell = m_cells.take(itemId)) {
        oldCell->deleteLater();
    }
    if (DashboardCell* newCell = createCell(*item)) {
        if (m_selectedItemId == itemId) {
            newCell->setSelected(true);
        }
    }
}

void DashboardGrid::applyRename(const QString& itemId, const QString& name) {
    DashboardItem* item = itemById(itemId);
    if (!item) {
        return;
    }
    item->name = name;
    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->setTitle(displayNameFor(*item));
    }
}

void DashboardGrid::applySetKey(const QString& itemId, const QString& key) {
    if (DashboardItem* item = itemById(itemId)) {
        item->key = key;
    }
}

QString DashboardGrid::displayNameFor(const DashboardItem& item) const {
    return item.name.isEmpty() ? WidgetRegistry::instance().displayName(item.typeId) : item.name;
}

bool DashboardGrid::isKeyAvailable(const QString& key, const QString& excludeId) const {
    if (key.isEmpty()) {
        return true;
    }
    for (const DashboardItem& item : m_items) {
        if (item.id != excludeId && item.key == key) {
            return false;
        }
    }
    return true;
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

    const int right = area.left() + area.width();
    const int bottom = area.top() + area.height();

    painter.setPen(QPen(palette.borderStrong, 1));
    for (int c = 0; c <= kGridColumns; ++c) {
        const int x = area.left() + qRound(c * area.width() / double(kGridColumns));
        painter.drawLine(x, area.top(), x, bottom);
    }
    for (int r = 0; r <= kGridRows; ++r) {
        const int y = area.top() + qRound(r * area.height() / double(kGridRows));
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
    const QRect area = rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    return QRect(area.left(), area.top(), qMax(1, area.width()), qMax(1, area.height()));
}

QRect DashboardGrid::itemRect(const DashboardItem& item) const {
    const QRect area = usableRect();
    const int left = area.left() + qRound(item.x * area.width());
    const int top = area.top() + qRound(item.y * area.height());
    const int width = qRound(item.width * area.width());
    const int height = qRound(item.height * area.height());
    return QRect(left, top, width, height);
}

void DashboardGrid::relayout() {
    for (const DashboardItem& item : m_items) {
        if (DashboardCell* cell = m_cells.value(item.id)) {
            cell->setGeometry(itemRect(item));
        }
    }
}

void DashboardGrid::relayoutItem(const QString& itemId) {
    if (const DashboardItem* item = itemById(itemId)) {
        if (DashboardCell* cell = m_cells.value(itemId)) {
            cell->setGeometry(itemRect(*item));
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

bool DashboardGrid::findFreeSlot(double width, double height, double* outX, double* outY) const {
    const int columnCells = qBound(1, qRound(width * kGridColumns), kGridColumns);
    const int rowCells = qBound(1, qRound(height * kGridRows), kGridRows);
    for (int r = 0; r + rowCells <= kGridRows; ++r) {
        for (int c = 0; c + columnCells <= kGridColumns; ++c) {
            DashboardItem probe;
            probe.x = c / double(kGridColumns);
            probe.y = r / double(kGridRows);
            probe.width = width;
            probe.height = height;
            if (isPlacementValid(probe, QString())) {
                *outX = probe.x;
                *outY = probe.y;
                return true;
            }
        }
    }
    return false;
}

bool DashboardGrid::isPlacementValid(const DashboardItem& candidate, const QString& excludeId) const {
    if (candidate.x < -kEpsilon || candidate.y < -kEpsilon) {
        return false;
    }
    if (candidate.x + candidate.width > 1.0 + kEpsilon || candidate.y + candidate.height > 1.0 + kEpsilon) {
        return false;
    }

    for (const DashboardItem& other : m_items) {
        if (other.id == excludeId) {
            continue;
        }
        const bool overlapsX =
            candidate.x < other.x + other.width - kEpsilon && other.x < candidate.x + candidate.width - kEpsilon;
        const bool overlapsY =
            candidate.y < other.y + other.height - kEpsilon && other.y < candidate.y + candidate.height - kEpsilon;
        if (overlapsX && overlapsY) {
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

    auto* cell = new DashboardCell(item.id, displayNameFor(item), content, this);
    cell->setEditMode(m_editMode);
    cell->setGeometry(itemRect(item));
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

    const QRect area = usableRect();
    const QPoint deltaPx = globalPos - m_drag->startGlobalPos;
    const int deltaColumnCells = qRound(deltaPx.x() / double(area.width()) * kGridColumns);
    const int deltaRowCells = qRound(deltaPx.y() / double(area.height()) * kGridRows);
    const double deltaX = deltaColumnCells / double(kGridColumns);
    const double deltaY = deltaRowCells / double(kGridRows);

    DashboardItem candidate = m_drag->original;
    candidate.x = qBound(0.0, m_drag->original.x + deltaX, qMax(0.0, 1.0 - candidate.width));
    candidate.y = qBound(0.0, m_drag->original.y + deltaY, qMax(0.0, 1.0 - candidate.height));
    m_drag->candidate = candidate;

    if (DashboardCell* draggedCell = m_cells.value(itemId)) {
        draggedCell->setGeometry(itemRect(candidate));
    }
}

void DashboardGrid::handleDragFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->itemId != itemId) {
        return;
    }
    if (isPlacementValid(m_drag->candidate, itemId)) {
        if (const DashboardItem* item = itemById(itemId)) {
            if (qAbs(item->x - m_drag->candidate.x) > kEpsilon || qAbs(item->y - m_drag->candidate.y) > kEpsilon) {
                m_undoStack->push(new MoveWidgetCommand(this, itemId, QPointF(item->x, item->y),
                                                         QPointF(m_drag->candidate.x, m_drag->candidate.y)));
            }
        }
    }
    m_drag.reset();
    relayoutItem(itemId);
}

void DashboardGrid::handleResizeStarted(const QString& itemId, const QPoint& globalPos,
                                         DashboardCell::ResizeHandle handle) {
    const DashboardItem* item = itemById(itemId);
    if (!item) {
        return;
    }
    m_drag = DragOp{itemId, globalPos, *item, *item, true, handle};
    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->raise();
    }
}

void DashboardGrid::handleResizeMoved(const QString& itemId, const QPoint& globalPos) {
    if (!m_drag || m_drag->itemId != itemId || !m_drag->resizing) {
        return;
    }

    const QRect area = usableRect();
    const QPoint deltaPx = globalPos - m_drag->startGlobalPos;
    const int deltaColumnCells = qRound(deltaPx.x() / double(area.width()) * kGridColumns);
    const int deltaRowCells = qRound(deltaPx.y() / double(area.height()) * kGridRows);
    const double deltaWidth = deltaColumnCells / double(kGridColumns);
    const double deltaHeight = deltaRowCells / double(kGridRows);

    // Edges resize a single dimension while anchoring the opposite edge;
    // corners resize both. Left/top anchor the position too, since their
    // opposite (right/bottom) edge is the one that must stay put.
    using Handle = DashboardCell::ResizeHandle;
    const bool resizesRight = m_drag->handle == Handle::Right || m_drag->handle == Handle::TopRight ||
                               m_drag->handle == Handle::BottomRight;
    const bool resizesLeft = m_drag->handle == Handle::Left || m_drag->handle == Handle::TopLeft ||
                              m_drag->handle == Handle::BottomLeft;
    const bool resizesBottom = m_drag->handle == Handle::Bottom || m_drag->handle == Handle::BottomLeft ||
                                m_drag->handle == Handle::BottomRight;
    const bool resizesTop = m_drag->handle == Handle::Top || m_drag->handle == Handle::TopLeft ||
                             m_drag->handle == Handle::TopRight;

    DashboardItem candidate = m_drag->original;

    if (resizesRight) {
        candidate.width = qBound(kMinItemWidth, m_drag->original.width + deltaWidth,
                                  qMax(kMinItemWidth, 1.0 - candidate.x));
    } else if (resizesLeft) {
        const double rightEdge = m_drag->original.x + m_drag->original.width;
        candidate.width =
            qBound(kMinItemWidth, m_drag->original.width - deltaWidth, qMax(kMinItemWidth, rightEdge));
        candidate.x = rightEdge - candidate.width;
    }

    if (resizesBottom) {
        candidate.height = qBound(kMinItemHeight, m_drag->original.height + deltaHeight,
                                   qMax(kMinItemHeight, 1.0 - candidate.y));
    } else if (resizesTop) {
        const double bottomEdge = m_drag->original.y + m_drag->original.height;
        candidate.height =
            qBound(kMinItemHeight, m_drag->original.height - deltaHeight, qMax(kMinItemHeight, bottomEdge));
        candidate.y = bottomEdge - candidate.height;
    }

    m_drag->candidate = candidate;

    if (DashboardCell* resizedCell = m_cells.value(itemId)) {
        resizedCell->setGeometry(itemRect(candidate));
    }
}

void DashboardGrid::handleResizeFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->itemId != itemId) {
        return;
    }
    if (isPlacementValid(m_drag->candidate, itemId)) {
        if (const DashboardItem* item = itemById(itemId)) {
            const QRectF fromGeometry(item->x, item->y, item->width, item->height);
            const QRectF toGeometry(m_drag->candidate.x, m_drag->candidate.y, m_drag->candidate.width,
                                     m_drag->candidate.height);
            const bool changed = qAbs(fromGeometry.x() - toGeometry.x()) > kEpsilon ||
                                  qAbs(fromGeometry.y() - toGeometry.y()) > kEpsilon ||
                                  qAbs(fromGeometry.width() - toGeometry.width()) > kEpsilon ||
                                  qAbs(fromGeometry.height() - toGeometry.height()) > kEpsilon;
            if (changed) {
                m_undoStack->push(new ResizeWidgetCommand(this, itemId, fromGeometry, toGeometry));
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
