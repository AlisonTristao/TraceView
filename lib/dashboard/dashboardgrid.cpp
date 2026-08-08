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
constexpr int kDefaultPrecision = 24;
constexpr int kMargin = 8;
constexpr int kGap = 3;
constexpr qreal kDefaultItemSize = 0.25;
constexpr qreal kEpsilon = 1e-6;
// Absolute floor for a widget's size, regardless of the current snap
// precision — at coarse precision (few, large cells) 1 cell can be most of
// the canvas; without this a resize would jump straight to that instead of
// something reasonably small.
constexpr qreal kMinItemFraction = 0.05;
} // namespace

DashboardGrid::DashboardGrid(QWidget* parent) : QWidget(parent), m_precision(kDefaultPrecision) {
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

void DashboardGrid::setPrecision(int precision) {
    precision = qMax(1, precision);
    if (precision == m_precision) {
        return;
    }
    // Deliberately does NOT touch m_items: item geometry is stored as
    // fractions of the canvas, independent of the snap grid's resolution.
    m_precision = precision;
    update();
    emit precisionChanged(m_precision);
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
    qreal x = 0;
    qreal y = 0;
    if (!findFreeSlot(kDefaultItemSize, kDefaultItemSize, &x, &y)) {
        x = 0;
        y = 0;
    }

    pushUndoSnapshot();

    DashboardItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.typeId = typeId;
    item.x = x;
    item.y = y;
    item.width = kDefaultItemSize;
    item.height = kDefaultItemSize;

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
    object["precision"] = m_precision;

    QJsonArray items;
    for (const DashboardItem& item : m_items) {
        items.append(dashboardItemToJson(item));
    }
    object["items"] = items;
    return object;
}

void DashboardGrid::fromJson(const QJsonObject& object) {
    clearItems();

    m_precision = qMax(1, object.value("precision").toInt(kDefaultPrecision));

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
    emit precisionChanged(m_precision);
}

QSize DashboardGrid::sizeHint() const {
    return contentSize();
}

QSize DashboardGrid::minimumSizeHint() const {
    return contentSize();
}

QSize DashboardGrid::contentSize() const {
    // A sane floor so the grid never collapses to zero — deliberately not
    // scaled by precision, since that's now user-configurable and can get
    // fairly large.
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

    const qreal cell = cellSizePx();
    // area.right()/bottom() are left+width-1 (Qt's QRect convention), but
    // item positions below are computed from left+width directly — use the
    // same convention here so grid lines line up exactly with item edges.
    const int right = area.left() + area.width();
    const int bottom = area.top() + area.height();

    for (qreal x = area.left(); x <= right + kEpsilon; x += cell) {
        painter.drawLine(qRound(x), area.top(), qRound(x), bottom);
    }
    for (qreal y = area.top(); y <= bottom + kEpsilon; y += cell) {
        painter.drawLine(area.left(), qRound(y), right, qRound(y));
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

qreal DashboardGrid::cellSizePx() const {
    return qMax(1.0, qreal(usableRect().width()) / m_precision);
}

QRect DashboardGrid::cellRect(const DashboardItem& item) const {
    const QRect area = usableRect();
    const int x = area.left() + qRound(item.x * area.width());
    const int y = area.top() + qRound(item.y * area.height());
    const int w = qRound(item.width * area.width());
    const int h = qRound(item.height * area.height());
    return QRect(x, y, w, h).adjusted(kGap, kGap, -kGap, -kGap);
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

bool DashboardGrid::findFreeSlot(qreal w, qreal h, qreal* outX, qreal* outY) const {
    // Only tries slots aligned to the current snap grid, so a newly added
    // widget always starts on-grid (the caller falls back to (0,0) — also
    // grid-aligned — if the grid has no free spot left).
    const qreal step = 1.0 / m_precision;
    for (qreal y = 0; y <= 1.0 - h + kEpsilon; y += step) {
        for (qreal x = 0; x <= 1.0 - w + kEpsilon; x += step) {
            DashboardItem probe;
            probe.x = x;
            probe.y = y;
            probe.width = w;
            probe.height = h;
            if (isPlacementValid(probe, QString())) {
                *outX = x;
                *outY = y;
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
        const bool overlapsX = candidate.x < other.x + other.width - kEpsilon && other.x < candidate.x + candidate.width - kEpsilon;
        const bool overlapsY = candidate.y < other.y + other.height - kEpsilon && other.y < candidate.y + candidate.height - kEpsilon;
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
    const qreal cell = cellSizePx();
    const QRect area = usableRect();

    // Snap the resulting ABSOLUTE position to the grid (not just the drag
    // delta) — the item's starting position isn't guaranteed to already be
    // grid-aligned, so snapping only the delta would preserve that original
    // offset forever instead of pulling it onto the grid.
    const qreal rawXPx = m_drag->original.x * area.width() + deltaPx.x();
    const qreal rawYPx = m_drag->original.y * area.height() + deltaPx.y();
    const qreal snappedXPx = qRound(rawXPx / cell) * cell;
    const qreal snappedYPx = qRound(rawYPx / cell) * cell;

    DashboardItem candidate = m_drag->original;
    candidate.x = qBound(0.0, snappedXPx / qMax(1, area.width()), 1.0 - candidate.width);
    candidate.y = qBound(0.0, snappedYPx / qMax(1, area.height()), 1.0 - candidate.height);
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
            if (qAbs(item->x - m_drag->candidate.x) > kEpsilon || qAbs(item->y - m_drag->candidate.y) > kEpsilon) {
                pushUndoSnapshot();
                item->x = m_drag->candidate.x;
                item->y = m_drag->candidate.y;
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
    const qreal cell = cellSizePx();
    const QRect area = usableRect();

    // Snap the resulting ABSOLUTE right/bottom edge to the grid (not just
    // the resize delta) — same reasoning as the drag case: the item's
    // starting edges aren't guaranteed to already be grid-aligned.
    const qreal xPx = m_drag->original.x * area.width();
    const qreal yPx = m_drag->original.y * area.height();
    const qreal rawRightPx = xPx + m_drag->original.width * area.width() + deltaPx.x();
    const qreal rawBottomPx = yPx + m_drag->original.height * area.height() + deltaPx.y();
    const qreal snappedRightPx = qRound(rawRightPx / cell) * cell;
    const qreal snappedBottomPx = qRound(rawBottomPx / cell) * cell;

    // qMin against kMinItemFraction: at coarse precision, 1 cell can be most
    // of the canvas — don't force the minimum size up that high.
    const qreal minWidth = qMin(kMinItemFraction, cell / qMax(1, area.width()));
    const qreal minHeight = qMin(kMinItemFraction, cell / qMax(1, area.height()));

    DashboardItem candidate = m_drag->original;
    candidate.width = qBound(minWidth, (snappedRightPx - xPx) / qMax(1, area.width()), 1.0 - candidate.x);
    candidate.height = qBound(minHeight, (snappedBottomPx - yPx) / qMax(1, area.height()), 1.0 - candidate.y);
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
            if (qAbs(item->width - m_drag->candidate.width) > kEpsilon ||
                qAbs(item->height - m_drag->candidate.height) > kEpsilon) {
                pushUndoSnapshot();
                item->width = m_drag->candidate.width;
                item->height = m_drag->candidate.height;
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
