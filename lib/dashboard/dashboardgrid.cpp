#include "dashboardgrid.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QRubberBand>
#include <QUuid>
#include <limits>

#include "dashboardcell.h"
#include "dashboardcommands.h"
#include "dashboardwidget.h"
#include "traceview/thememanager.h"
#include "widgetregistry.h"

namespace traceview {

namespace {
// Both the canvas-edge margin and the gap between two adjacent widgets
// resolve to the same pixel value, computed as a fraction of the canvas's
// *shorter* side (not width/height independently) — that's what keeps the
// on-screen distance identical horizontally and vertically on a non-square
// canvas, and lets it scale with the window instead of sitting at a fixed
// pixel count. usableRect() insets by half of it and itemRect() insets each
// item by the other half, so two touching items end up exactly kGutterPx
// apart, same as an item sitting against the canvas edge.
constexpr double kGutterFraction = 0.01;
constexpr int kMinGutter = 8;
constexpr int kMaxGutter = 32;
// Custom clipboard format for copySelected()/pasteItem() — carries a single
// dashboardItemToJson() object, so paste can reuse the same (de)serializer
// as project save/load instead of a bespoke copy of DashboardItem's fields.
constexpr const char* kClipboardMimeType = "application/x-traceview-dashboarditem+json";
// Fixed logical division count driving grid-line painting and drag/resize
// snap granularity. Deliberately independent of window size — items are
// stored as fractions of the canvas (see DashboardItem), so this constant
// only shapes interaction feel, never item geometry directly. That's what
// keeps relayout() resize-safe: there is no per-window column/row count to
// go stale and clamp items against.
constexpr int kGridColumns = 60;
constexpr int kGridRows = 40;
// Radius of the small dots painted at each grid node in edit mode (see
// paintEvent()) — deliberately subtle, just a hint of the snap points.
// Kept small relative to the grid density above so denser dots don't read
// as visual clutter.
constexpr double kGridDotRadius = 0.9;
// The outermost this-many rings of dots fade toward zero alpha instead of
// stopping at full opacity right at usableRect's edge (see paintEvent()) —
// makes the grid dissolve into the margin instead of a hard cutoff.
constexpr int kGridEdgeFadeCells = 2;
// A widget below this fraction in either dimension is too small to be
// usable (header alone is 24px). Mirrors the old 5-cell minimum.
constexpr double kMinItemWidth = 5.0 / kGridColumns;
constexpr double kMinItemHeight = 5.0 / kGridRows;
// Header-less kinds (DashboardWidget::wantsCellHeader() == false — the
// control types: push button/toggle/slider, see widgets/controlwidgets.h)
// have no 24px header eating into the cell, so they can shrink much closer
// to "just the control itself" than the default minimum above.
constexpr double kMinHeaderlessItemWidth = 2.0 / kGridColumns;
constexpr double kMinHeaderlessItemHeight = 2.0 / kGridRows;
constexpr double kDefaultItemWidth = 16.0 / kGridColumns;
constexpr double kDefaultItemHeight = 12.0 / kGridRows;
// Smaller starting footprint for the same header-less control kinds
// referenced above (push button/toggle/slider) -- the default sized for a
// chart/gauge/serial monitor above reads as oversized for a single small
// control. Roughly double kMinHeaderlessItemWidth/Height, half of
// kDefaultItemWidth/Height -- big enough to comfortably show the control,
// small enough not to dominate the canvas the way the chart default does.
constexpr double kDefaultHeaderlessItemWidth = 8.0 / kGridColumns;
constexpr double kDefaultHeaderlessItemHeight = 6.0 / kGridRows;
// Tolerance for fraction comparisons (bounds/overlap checks), to absorb
// floating-point rounding from snapping math without treating touching
// edges as overlapping.
constexpr double kEpsilon = 1e-6;
}  // namespace

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
    // gutter() no longer depends on m_editMode, so cell rects are unchanged
    // by the toggle itself; update() alone repaints the edit-only chrome
    // (grid dots, slot-guide outlines) and each cell's own header/grip.
    update();
}

void DashboardGrid::setDeviceConnected(const QString& deviceId, bool connected) {
    if (deviceId.isEmpty()) {
        return;
    }
    if (m_deviceConnectionStates.contains(deviceId) &&
        m_deviceConnectionStates.value(deviceId) == connected) {
        return;
    }
    m_deviceConnectionStates[deviceId] = connected;

    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const DashboardItem* item = itemById(it.key());
        if (item && item->config.value("deviceId").toString() == deviceId) {
            it.value()->setConnected(connected);
        }
    }
}

QJsonObject DashboardGrid::configForWidget(DashboardWidget* widget) const {
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        if (it.value()->content() == widget) {
            if (const DashboardItem* item = itemById(it.key())) {
                return item->config;
            }
        }
    }
    return QJsonObject();
}

QSet<QString> DashboardGrid::expandGroups(const QSet<QString>& ids) const {
    QSet<QString> groupIds;
    for (const QString& id : ids) {
        if (const DashboardItem* item = itemById(id)) {
            if (!item->groupId.isEmpty()) {
                groupIds.insert(item->groupId);
            }
        }
    }
    if (groupIds.isEmpty()) {
        return ids;
    }

    QSet<QString> result = ids;
    for (const DashboardItem& item : m_items) {
        if (!item.groupId.isEmpty() && groupIds.contains(item.groupId)) {
            result.insert(item.id);
        }
    }
    return result;
}

void DashboardGrid::applySelection(const QSet<QString>& target) {
    if (target == m_selectedItemIds) {
        return;
    }
    for (const QString& id : m_selectedItemIds) {
        if (!target.contains(id)) {
            if (DashboardCell* cell = m_cells.value(id)) {
                cell->setSelected(false);
            }
        }
    }
    m_selectedItemIds = target;
    for (const QString& id : m_selectedItemIds) {
        if (DashboardCell* cell = m_cells.value(id)) {
            cell->setSelected(true);
        }
    }
    updateResizableFlags();
    // Undoes any temporary raise the previous selection got below, restoring
    // the persisted layer order, before (maybe) temporarily raising the new
    // selection -- purely visual, m_items' order is untouched either way.
    restackCells();
    for (const DashboardItem& item : m_items) {
        if (m_selectedItemIds.contains(item.id)) {
            if (DashboardCell* cell = m_cells.value(item.id)) {
                cell->raise();
            }
        }
    }
    emit selectionChanged(selectedItemId());
}

void DashboardGrid::updateResizableFlags() {
    // Multi-selected/grouped cells can be dragged as a unit but not resized
    // from that state -- resizing N items at once has no obvious single
    // meaning, so it's simply disabled until the selection is back to one.
    const bool resizable = m_selectedItemIds.size() <= 1;
    for (const QString& id : m_selectedItemIds) {
        if (DashboardCell* cell = m_cells.value(id)) {
            cell->setResizable(resizable);
        }
    }
}

void DashboardGrid::selectItem(const QString& itemId) {
    QSet<QString> target;
    if (!itemId.isEmpty()) {
        target = expandGroups({itemId});
    }
    applySelection(target);
}

void DashboardGrid::toggleItemSelection(const QString& itemId) {
    if (itemId.isEmpty()) {
        return;
    }
    const QSet<QString> unit = expandGroups({itemId});
    QSet<QString> target = m_selectedItemIds;
    if (target.contains(itemId)) {
        target.subtract(unit);
    } else {
        target.unite(unit);
    }
    applySelection(target);
}

void DashboardGrid::selectItems(const QSet<QString>& ids, bool add) {
    const QSet<QString> expanded = expandGroups(ids);
    applySelection(add ? (m_selectedItemIds + expanded) : expanded);
}

QString DashboardGrid::selectedItemId() const {
    return m_selectedItemIds.size() == 1 ? *m_selectedItemIds.constBegin() : QString();
}

bool DashboardGrid::selectionHasGroup() const {
    for (const QString& id : m_selectedItemIds) {
        if (const DashboardItem* item = itemById(id)) {
            if (!item->groupId.isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

QString DashboardGrid::selectedItemTypeId() const {
    if (const DashboardItem* item = itemById(selectedItemId())) {
        return item->typeId;
    }
    return QString();
}

QString DashboardGrid::selectedItemDisplayName() const {
    if (const DashboardItem* item = itemById(selectedItemId())) {
        return displayNameFor(*item);
    }
    return QString();
}

QString DashboardGrid::selectedItemKey() const {
    if (const DashboardItem* item = itemById(selectedItemId())) {
        return item->key;
    }
    return QString();
}

QJsonObject DashboardGrid::selectedItemConfig() const {
    if (const DashboardItem* item = itemById(selectedItemId())) {
        return item->config;
    }
    return QJsonObject();
}

void DashboardGrid::addItem(const QString& typeId) {
    // wantsCellHeader() is only known once an instance exists, so a
    // throwaway probe (same WidgetRegistry::create() createCell() below
    // will use for real) decides which default footprint applies -- keeps
    // "which kinds are compact" defined in exactly one place (DashboardWidget::
    // wantsCellHeader()) instead of a second typeId list here that could
    // drift out of sync with widgets/controlwidgets.h.
    bool headerless = false;
    if (DashboardWidget* probe = WidgetRegistry::instance().create(typeId, nullptr)) {
        headerless = !probe->wantsCellHeader();
        delete probe;
    }
    const double width = headerless ? kDefaultHeaderlessItemWidth : kDefaultItemWidth;
    const double height = headerless ? kDefaultHeaderlessItemHeight : kDefaultItemHeight;

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
    if (m_selectedItemIds.isEmpty()) {
        return;
    }
    if (m_selectedItemIds.size() == 1) {
        if (const DashboardItem* item = itemById(*m_selectedItemIds.constBegin())) {
            m_undoStack->push(new RemoveWidgetCommand(this, *item));
        }
        return;
    }

    // Collected in model order (not selection-set order) so undo reinserts
    // them in a stable, predictable sequence -- same reasoning as why a
    // single removed item is simply appended back on undo.
    QVector<DashboardItem> items;
    for (const DashboardItem& item : m_items) {
        if (m_selectedItemIds.contains(item.id)) {
            items.append(item);
        }
    }
    if (!items.isEmpty()) {
        m_undoStack->push(new RemoveWidgetsCommand(this, items));
    }
}

void DashboardGrid::copySelected() const {
    const DashboardItem* item = itemById(selectedItemId());
    if (!item) {
        return;
    }
    const QJsonDocument doc(dashboardItemToJson(*item));
    auto* mimeData = new QMimeData();
    mimeData->setData(kClipboardMimeType, doc.toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(mimeData);
}

bool DashboardGrid::canPaste() const {
    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    return mimeData && mimeData->hasFormat(kClipboardMimeType);
}

void DashboardGrid::pasteItem() {
    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasFormat(kClipboardMimeType)) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(mimeData->data(kClipboardMimeType), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    bool ok = false;
    DashboardItem item = dashboardItemFromJson(doc.object(), &ok);
    if (!ok || WidgetRegistry::instance().displayName(item.typeId).isEmpty()) {
        return;
    }

    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Keys must stay unique — the pasted copy starts unkeyed, same as a
    // brand-new item added via addItem(). Group membership doesn't carry
    // over either -- otherwise a pasted copy would silently weld itself
    // into the original's group instead of standing alone.
    item.key.clear();
    item.groupId.clear();

    // Prefer landing one cell down-right of the copied spot (reads as "a
    // copy placed next to the original"); fall back to the first free cell
    // like addItem() does if that spot is occupied or off-canvas.
    DashboardItem offsetCandidate = item;
    offsetCandidate.x = item.x + 1.0 / kGridColumns;
    offsetCandidate.y = item.y + 1.0 / kGridRows;
    if (isPlacementFree(offsetCandidate, QString())) {
        item.x = offsetCandidate.x;
        item.y = offsetCandidate.y;
    } else if (!findFreeSlot(item.width, item.height, &item.x, &item.y)) {
        item.x = 0.0;
        item.y = 0.0;
    }

    m_undoStack->push(new AddWidgetCommand(this, item));
    selectItem(item.id);
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
    update();
    emit itemsChanged();
}

void DashboardGrid::changeSelectedType(const QString& newTypeId) {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const DashboardItem* item = itemById(id);
    if (!item || item->typeId == newTypeId ||
        WidgetRegistry::instance().displayName(newTypeId).isEmpty()) {
        return;
    }

    m_undoStack->push(new ChangeWidgetTypeCommand(this, id, item->typeId, newTypeId));
}

void DashboardGrid::renameSelected(const QString& newName) {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const DashboardItem* item = itemById(id);
    if (!item || item->name == newName) {
        return;
    }

    m_undoStack->push(new RenameWidgetCommand(this, id, item->name, newName));
}

bool DashboardGrid::setSelectedKey(const QString& newKey) {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return false;
    }
    const DashboardItem* item = itemById(id);
    if (!item) {
        return false;
    }
    if (item->key == newKey) {
        return true;
    }
    if (!isKeyAvailable(newKey, id)) {
        return false;
    }

    m_undoStack->push(new SetItemKeyCommand(this, id, item->key, newKey));
    return true;
}

void DashboardGrid::changeSelectedConfig(const QJsonObject& newConfig) {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const DashboardItem* item = itemById(id);
    if (!item || item->config == newConfig) {
        return;
    }

    m_undoStack->push(new SetItemConfigCommand(this, id, item->config, newConfig));
}

void DashboardGrid::bringSelectedToFront() {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const int from = indexOfItem(id);
    const int to = m_items.size() - 1;
    if (from < 0 || from == to) {
        return;
    }
    m_undoStack->push(new ChangeZOrderCommand(this, id, from, to, tr("Bring to Front")));
}

void DashboardGrid::bringSelectedForward() {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const int from = indexOfItem(id);
    if (from < 0) {
        return;
    }
    const int to = qMin(from + 1, m_items.size() - 1);
    if (from == to) {
        return;
    }
    m_undoStack->push(new ChangeZOrderCommand(this, id, from, to, tr("Bring Forward")));
}

void DashboardGrid::sendSelectedBackward() {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const int from = indexOfItem(id);
    if (from < 0) {
        return;
    }
    const int to = qMax(from - 1, 0);
    if (from == to) {
        return;
    }
    m_undoStack->push(new ChangeZOrderCommand(this, id, from, to, tr("Send Backward")));
}

void DashboardGrid::sendSelectedToBack() {
    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return;
    }
    const int from = indexOfItem(id);
    if (from <= 0) {
        return;
    }
    m_undoStack->push(new ChangeZOrderCommand(this, id, from, 0, tr("Send to Back")));
}

void DashboardGrid::groupSelected() {
    if (m_selectedItemIds.size() < 2) {
        return;
    }
    QMap<QString, QString> previousGroupIds;
    for (const QString& id : m_selectedItemIds) {
        if (const DashboardItem* item = itemById(id)) {
            previousGroupIds.insert(id, item->groupId);
        }
    }
    const QString newGroupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_undoStack->push(new GroupItemsCommand(this, previousGroupIds, newGroupId));
}

void DashboardGrid::ungroupSelected() {
    QMap<QString, QString> previousGroupIds;
    for (const QString& id : m_selectedItemIds) {
        if (const DashboardItem* item = itemById(id)) {
            if (!item->groupId.isEmpty()) {
                previousGroupIds.insert(id, item->groupId);
            }
        }
    }
    if (previousGroupIds.isEmpty()) {
        return;
    }
    m_undoStack->push(new UngroupItemsCommand(this, previousGroupIds));
}

void DashboardGrid::applyInsertItem(const DashboardItem& item) {
    m_items.append(item);
    createCell(item);
    updateGeometry();
    update();
    emit itemsChanged();
}

void DashboardGrid::applyRemoveItemById(const QString& itemId) {
    // Whenever this fires for a currently-selected id, it's either the sole
    // selected item (single-item remove) or one of several being removed
    // together in the same RemoveWidgetsCommand (which iterates the whole
    // selected set) -- either way, clearing the whole selection here is
    // correct, not just premature, since every other selected id is about
    // to be removed too.
    if (m_selectedItemIds.contains(itemId)) {
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
        if (m_selectedItemIds.contains(itemId)) {
            newCell->setSelected(true);
        }
    }
    emit itemsChanged();
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
        emit itemsChanged();
    }
}

void DashboardGrid::applySetConfig(const QString& itemId, const QJsonObject& config) {
    if (DashboardItem* item = itemById(itemId)) {
        item->config = config;
        if (DashboardCell* cell = m_cells.value(itemId)) {
            if (DashboardWidget* content = cell->content()) {
                content->setConfig(config);
            }
            // The edit may have changed which device this widget targets --
            // re-derive the dot from that device's last-known state rather
            // than leaving it showing the previous device's.
            const QString deviceId = config.value("deviceId").toString();
            cell->setConnected(!deviceId.isEmpty() &&
                               m_deviceConnectionStates.value(deviceId, false));
        }
    }
}

void DashboardGrid::applyZOrder(const QString& itemId, int index) {
    const int from = indexOfItem(itemId);
    if (from < 0) {
        return;
    }
    index = qBound(0, index, m_items.size() - 1);
    if (from == index) {
        return;
    }
    m_items.move(from, index);
    restackCells();
}

void DashboardGrid::applySetGroup(const QString& itemId, const QString& groupId) {
    if (DashboardItem* item = itemById(itemId)) {
        item->groupId = groupId;
    }
}

void DashboardGrid::restackCells() {
    for (const DashboardItem& item : m_items) {
        if (DashboardCell* cell = m_cells.value(item.id)) {
            cell->raise();
        }
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

QMap<QString, DashboardWidget*> DashboardGrid::keyedWidgets() const {
    QMap<QString, DashboardWidget*> result;
    for (const DashboardItem& item : m_items) {
        if (item.key.isEmpty()) {
            continue;
        }
        if (DashboardCell* cell = m_cells.value(item.id)) {
            result.insert(item.key, cell->content());
        }
    }
    return result;
}

QVector<DashboardLayerEntry> DashboardGrid::layerEntries() const {
    QVector<DashboardLayerEntry> result;
    result.reserve(m_items.size());
    for (const DashboardItem& item : m_items) {
        result.append({item.id, displayNameFor(item)});
    }
    return result;
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
    emit itemsChanged();
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
    if (!m_editMode) {
        return;
    }

    QPainter painter(this);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = usableRect();

    // Square outline at each item's full slot (pre-gutter-inset) so the
    // gutter's breathing room doesn't hide where the cell actually snaps to
    // — the cell itself (drawn on top, as a child widget) sits inset from
    // this by half a gutter on every side. Square corners on purpose: this
    // is an alignment guide, not part of the visual design.
    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    for (const DashboardItem& item : m_items) {
        // Track the live candidate rect while this item is being dragged or
        // resized, same as the cell itself (see handleDragMoved()/
        // handleResizeMoved()) -- otherwise the guide lags behind at the
        // item's pre-drag position instead of following the cell around.
        // Every dragged/resized item has its own candidates entry (not just
        // the primary one the mouse is tracking), so a multi/group drag's
        // guide outlines all follow their cells instead of only the primary.
        const bool isDragCandidate = m_drag && m_drag->candidates.contains(item.id);
        painter.drawRect(slotRect(isDragCandidate ? m_drag->candidates.value(item.id) : item));
    }

    // Small gray dots at each grid node instead of full lines — enough to
    // hint at the snap points while editing without the visual clutter of a
    // crosshatch over widget content. The outermost couple of rings fade
    // toward zero alpha instead of stopping at full opacity right at
    // usableRect's edge, so the grid dissolves into the margin instead of
    // cutting off mid-dot.
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    for (int c = 0; c <= kGridColumns; ++c) {
        const int x = area.left() + qRound(c * area.width() / double(kGridColumns));
        const int edgeDistanceX = qMin(c, kGridColumns - c);
        for (int r = 0; r <= kGridRows; ++r) {
            const int y = area.top() + qRound(r * area.height() / double(kGridRows));
            const int edgeDistance = qMin(edgeDistanceX, qMin(r, kGridRows - r));
            QColor dotColor = palette.textDisabled;
            if (edgeDistance < kGridEdgeFadeCells) {
                dotColor.setAlphaF(dotColor.alphaF() * (edgeDistance + 1) /
                                   double(kGridEdgeFadeCells + 1));
            }
            painter.setBrush(dotColor);
            painter.drawEllipse(QPointF(x, y), kGridDotRadius, kGridDotRadius);
        }
    }
}

void DashboardGrid::mousePressEvent(QMouseEvent* event) {
    // Reaches us only for clicks that missed every cell (Qt delivers to the
    // topmost child widget directly otherwise) — i.e. empty grid space.
    // Deferred: we don't yet know if this is a plain click (clears the
    // selection, same as always) or the start of a rubber-band drag -- that
    // decision happens in mouseMoveEvent()/mouseReleaseEvent() once we know
    // whether the press actually moved.
    if (m_editMode && event->button() == Qt::LeftButton) {
        m_rubberBandOrigin = event->position().toPoint();
        m_rubberBandAdditive = event->modifiers().testFlag(Qt::ControlModifier);
        m_rubberBandPending = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DashboardGrid::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBandPending) {
        const QPoint pos = event->position().toPoint();
        if (!m_rubberBand &&
            (pos - m_rubberBandOrigin).manhattanLength() >= QApplication::startDragDistance()) {
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        }
        if (m_rubberBand) {
            m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, pos).normalized());
            m_rubberBand->show();
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void DashboardGrid::mouseReleaseEvent(QMouseEvent* event) {
    if (m_rubberBandPending) {
        m_rubberBandPending = false;
        if (m_rubberBand) {
            const QRect bandRect = m_rubberBand->geometry();
            m_rubberBand->hide();
            m_rubberBand->deleteLater();
            m_rubberBand = nullptr;

            QSet<QString> hits;
            for (const DashboardItem& item : m_items) {
                if (itemRect(item).intersects(bandRect)) {
                    hits.insert(item.id);
                }
            }
            selectItems(hits, m_rubberBandAdditive);
        } else {
            // Never exceeded the drag threshold -- a plain click on empty
            // space, same as before this feature existed: clear selection.
            selectItem(QString());
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

int DashboardGrid::gutter() const {
    return qBound(kMinGutter, qRound(qMin(width(), height()) * kGutterFraction), kMaxGutter);
}

QRect DashboardGrid::usableRect() const {
    const int half = gutter() / 2;
    const QRect area = rect().adjusted(half, half, -half, -half);
    return QRect(area.left(), area.top(), qMax(1, area.width()), qMax(1, area.height()));
}

QRect DashboardGrid::slotRect(const DashboardItem& item) const {
    const QRect area = usableRect();
    const int left = area.left() + qRound(item.x * area.width());
    const int top = area.top() + qRound(item.y * area.height());
    const int width = qRound(item.width * area.width());
    const int height = qRound(item.height * area.height());
    return QRect(left, top, width, height);
}

QRect DashboardGrid::itemRect(const DashboardItem& item) const {
    const int half = gutter() / 2;
    return slotRect(item).adjusted(half, half, -half, -half);
}

void DashboardGrid::relayout() {
    for (const DashboardItem& item : m_items) {
        if (DashboardCell* cell = m_cells.value(item.id)) {
            cell->setGeometry(itemRect(item));
        }
    }
    // The slot-guide outline (see paintEvent()) sits in the gutter just
    // outside each cell's own rect, so moving/resizing a cell doesn't touch
    // any pixel Qt would think to repaint on its own — force it explicitly
    // instead of relying on the child-geometry-change auto-invalidation,
    // which only covers the cell's own (smaller) old/new rect.
    update();
}

void DashboardGrid::relayoutItem(const QString& itemId) {
    if (const DashboardItem* item = itemById(itemId)) {
        if (DashboardCell* cell = m_cells.value(itemId)) {
            cell->setGeometry(itemRect(*item));
        }
    }
    update();
}

void DashboardGrid::clearItems() {
    qDeleteAll(m_cells);
    m_cells.clear();
    m_items.clear();
    m_drag.reset();
    m_selectedItemIds.clear();
    if (m_rubberBand) {
        m_rubberBand->deleteLater();
        m_rubberBand = nullptr;
    }
    m_rubberBandPending = false;
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
            if (isPlacementFree(probe, QString())) {
                *outX = probe.x;
                *outY = probe.y;
                return true;
            }
        }
    }
    return false;
}

bool DashboardGrid::isPlacementValid(const DashboardItem& candidate, const QString&) const {
    if (candidate.x < -kEpsilon || candidate.y < -kEpsilon) {
        return false;
    }
    if (candidate.x + candidate.width > 1.0 + kEpsilon ||
        candidate.y + candidate.height > 1.0 + kEpsilon) {
        return false;
    }
    return true;
}

bool DashboardGrid::isPlacementFree(const DashboardItem& candidate,
                                    const QString& excludeId) const {
    if (!isPlacementValid(candidate, excludeId)) {
        return false;
    }

    for (const DashboardItem& other : m_items) {
        if (other.id == excludeId) {
            continue;
        }
        const bool overlapsX = candidate.x < other.x + other.width - kEpsilon &&
                               other.x < candidate.x + candidate.width - kEpsilon;
        const bool overlapsY = candidate.y < other.y + other.height - kEpsilon &&
                               other.y < candidate.y + candidate.height - kEpsilon;
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

int DashboardGrid::indexOfItem(const QString& itemId) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == itemId) {
            return i;
        }
    }
    return -1;
}

DashboardCell* DashboardGrid::createCell(const DashboardItem& item) {
    DashboardWidget* content = WidgetRegistry::instance().create(item.typeId, nullptr);
    if (!content) {
        return nullptr;
    }
    content->setConfig(item.config);

    auto* cell = new DashboardCell(item.id, item.typeId, displayNameFor(item), content, this);
    cell->setEditMode(m_editMode);
    const QString deviceId = item.config.value("deviceId").toString();
    cell->setConnected(!deviceId.isEmpty() && m_deviceConnectionStates.value(deviceId, false));
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
    emit widgetCreated(content);
    return cell;
}

void DashboardGrid::handleDragStarted(const QString& itemId, const QPoint& globalPos) {
    if (!itemById(itemId)) {
        return;
    }
    // Dragging a member of the current multi-selection/group moves every
    // member together; dragging anything else (shouldn't normally happen,
    // since a cell only starts a drag once it's already selected -- see
    // DashboardCell::mousePressEvent) falls back to just that one item.
    const QSet<QString> members =
        m_selectedItemIds.contains(itemId) ? m_selectedItemIds : QSet<QString>{itemId};

    DragOp op;
    op.primaryItemId = itemId;
    op.startGlobalPos = globalPos;
    op.resizing = false;
    for (const QString& id : members) {
        if (const DashboardItem* member = itemById(id)) {
            op.originals.insert(id, *member);
            op.candidates.insert(id, *member);
        }
    }
    m_drag = std::move(op);

    for (auto it = m_drag->originals.constBegin(); it != m_drag->originals.constEnd(); ++it) {
        if (DashboardCell* cell = m_cells.value(it.key())) {
            cell->raise();
            cell->setDragInvalid(false);
        }
    }
}

void DashboardGrid::handleDragMoved(const QString& itemId, const QPoint& globalPos) {
    if (!m_drag || m_drag->primaryItemId != itemId || m_drag->resizing) {
        return;
    }

    const QRect area = usableRect();
    const QPoint deltaPx = globalPos - m_drag->startGlobalPos;
    const int deltaColumnCells = qRound(deltaPx.x() / double(area.width()) * kGridColumns);
    const int deltaRowCells = qRound(deltaPx.y() / double(area.height()) * kGridRows);
    double deltaX = deltaColumnCells / double(kGridColumns);
    double deltaY = deltaRowCells / double(kGridRows);

    // Clamp the delta itself (not each member's candidate independently) so
    // a multi/group drag stays rigid: every member keeps its exact pre-drag
    // offset from the others, even if one of them would hit the canvas edge
    // before the rest do.
    double loDeltaX = -std::numeric_limits<double>::infinity();
    double hiDeltaX = std::numeric_limits<double>::infinity();
    double loDeltaY = -std::numeric_limits<double>::infinity();
    double hiDeltaY = std::numeric_limits<double>::infinity();
    for (auto it = m_drag->originals.constBegin(); it != m_drag->originals.constEnd(); ++it) {
        const DashboardItem& original = it.value();
        loDeltaX = qMax(loDeltaX, -original.x);
        hiDeltaX = qMin(hiDeltaX, qMax(0.0, 1.0 - original.width) - original.x);
        loDeltaY = qMax(loDeltaY, -original.y);
        hiDeltaY = qMin(hiDeltaY, qMax(0.0, 1.0 - original.height) - original.y);
    }
    // Defensive only -- shouldn't trigger for a selection that was already
    // entirely on-canvas before the drag started.
    if (loDeltaX <= hiDeltaX) {
        deltaX = qBound(loDeltaX, deltaX, hiDeltaX);
    }
    if (loDeltaY <= hiDeltaY) {
        deltaY = qBound(loDeltaY, deltaY, hiDeltaY);
    }

    for (auto it = m_drag->originals.constBegin(); it != m_drag->originals.constEnd(); ++it) {
        const QString& id = it.key();
        DashboardItem candidate = it.value();
        candidate.x = it.value().x + deltaX;
        candidate.y = it.value().y + deltaY;
        m_drag->candidates[id] = candidate;

        if (DashboardCell* cell = m_cells.value(id)) {
            cell->setGeometry(itemRect(candidate));
            // Live feedback for a candidate that would be rejected on
            // release -- otherwise the cell(s) would silently snap back to
            // their original spot with no warning.
            cell->setDragInvalid(!isPlacementValid(candidate, id));
        }
    }
    update();
}

void DashboardGrid::handleDragFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->primaryItemId != itemId) {
        return;
    }

    // All-or-nothing, same as a single-item drag: if any member's candidate
    // would be rejected, commit nothing and let every dragged cell snap back
    // to its unchanged model position below, instead of partially applying
    // the move and desyncing the group.
    bool allValid = true;
    for (auto it = m_drag->candidates.constBegin(); it != m_drag->candidates.constEnd(); ++it) {
        if (!isPlacementValid(it.value(), it.key())) {
            allValid = false;
            break;
        }
    }

    if (allValid) {
        QMap<QString, QPointF> fromPositions;
        QMap<QString, QPointF> toPositions;
        for (auto it = m_drag->originals.constBegin(); it != m_drag->originals.constEnd(); ++it) {
            const QString& id = it.key();
            const DashboardItem& original = it.value();
            const DashboardItem& candidate = m_drag->candidates.value(id);
            if (qAbs(original.x - candidate.x) > kEpsilon ||
                qAbs(original.y - candidate.y) > kEpsilon) {
                fromPositions.insert(id, QPointF(original.x, original.y));
                toPositions.insert(id, QPointF(candidate.x, candidate.y));
            }
        }
        if (!fromPositions.isEmpty()) {
            m_undoStack->push(new MoveWidgetsCommand(this, fromPositions, toPositions));
        }
    }

    const QStringList draggedIds = m_drag->originals.keys();
    m_drag.reset();
    for (const QString& id : draggedIds) {
        relayoutItem(id);
        if (DashboardCell* cell = m_cells.value(id)) {
            cell->setDragInvalid(false);
        }
    }
}

void DashboardGrid::handleResizeStarted(const QString& itemId, const QPoint& globalPos,
                                        DashboardCell::ResizeHandle handle) {
    const DashboardItem* item = itemById(itemId);
    if (!item) {
        return;
    }
    DragOp op;
    op.primaryItemId = itemId;
    op.startGlobalPos = globalPos;
    op.originals.insert(itemId, *item);
    op.candidates.insert(itemId, *item);
    op.resizing = true;
    op.handle = handle;
    m_drag = std::move(op);

    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->raise();
        cell->setDragInvalid(false);
    }
}

void DashboardGrid::handleResizeMoved(const QString& itemId, const QPoint& globalPos) {
    if (!m_drag || m_drag->primaryItemId != itemId || !m_drag->resizing) {
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
    const bool resizesRight = m_drag->handle == Handle::Right ||
                              m_drag->handle == Handle::TopRight ||
                              m_drag->handle == Handle::BottomRight;
    const bool resizesLeft = m_drag->handle == Handle::Left || m_drag->handle == Handle::TopLeft ||
                             m_drag->handle == Handle::BottomLeft;
    const bool resizesBottom = m_drag->handle == Handle::Bottom ||
                               m_drag->handle == Handle::BottomLeft ||
                               m_drag->handle == Handle::BottomRight;
    const bool resizesTop = m_drag->handle == Handle::Top || m_drag->handle == Handle::TopLeft ||
                            m_drag->handle == Handle::TopRight;

    const DashboardItem original = m_drag->originals.value(itemId);
    DashboardItem candidate = original;

    const DashboardCell* resizingCell = m_cells.value(itemId);
    const bool compact = resizingCell && !resizingCell->hasHeader();
    const double minWidth = compact ? kMinHeaderlessItemWidth : kMinItemWidth;
    const double minHeight = compact ? kMinHeaderlessItemHeight : kMinItemHeight;

    if (resizesRight) {
        candidate.width =
            qBound(minWidth, original.width + deltaWidth, qMax(minWidth, 1.0 - candidate.x));
    } else if (resizesLeft) {
        const double rightEdge = original.x + original.width;
        candidate.width = qBound(minWidth, original.width - deltaWidth, qMax(minWidth, rightEdge));
        candidate.x = rightEdge - candidate.width;
    }

    if (resizesBottom) {
        candidate.height =
            qBound(minHeight, original.height + deltaHeight, qMax(minHeight, 1.0 - candidate.y));
    } else if (resizesTop) {
        const double bottomEdge = original.y + original.height;
        candidate.height =
            qBound(minHeight, original.height - deltaHeight, qMax(minHeight, bottomEdge));
        candidate.y = bottomEdge - candidate.height;
    }

    m_drag->candidates[itemId] = candidate;

    if (DashboardCell* resizedCell = m_cells.value(itemId)) {
        resizedCell->setGeometry(itemRect(candidate));
        // Same live rejection feedback as handleDragMoved() above.
        resizedCell->setDragInvalid(!isPlacementValid(candidate, itemId));
    }
    update();
}

void DashboardGrid::handleResizeFinished(const QString& itemId, const QPoint&) {
    if (!m_drag || m_drag->primaryItemId != itemId) {
        return;
    }
    const DashboardItem candidate = m_drag->candidates.value(itemId);
    if (isPlacementValid(candidate, itemId)) {
        if (const DashboardItem* item = itemById(itemId)) {
            const QRectF fromGeometry(item->x, item->y, item->width, item->height);
            const QRectF toGeometry(candidate.x, candidate.y, candidate.width, candidate.height);
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
    if (DashboardCell* cell = m_cells.value(itemId)) {
        cell->setDragInvalid(false);
    }
}

void DashboardGrid::handleSelectRequested(const QString& itemId, Qt::KeyboardModifiers modifiers) {
    if (modifiers.testFlag(Qt::ControlModifier)) {
        toggleItemSelection(itemId);
    } else {
        selectItem(itemId);
    }
}

}  // namespace traceview
