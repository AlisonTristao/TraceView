#include "dashboardcell.h"

#include <QAction>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegion>
#include <QWidgetAction>

#include "dashboardwidget.h"
#include "dashboard/roundedcorners.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kHeaderHeight = 24;
constexpr int kGripSize = 14;   // hit/visual size of the 4 corner handles
constexpr int kEdgeMargin = 6;  // hit thickness of the 4 edge handles
constexpr int kSelectionAnimMs = 150;

constexpr int kIconSize = 14;
constexpr int kIconMargin = 6;
// Header-controls cluster (see DashboardWidget::wantsHeaderControls()): the
// connection dot before the title, and the pause/resume + clear + gear
// buttons at the header's right edge -- all sized/spaced off the same
// kIconSize/kIconMargin as the type glyph above, per the "respect the
// widgets' header bar size" ask, not the ribbon's separate 16px/26px scale
// (see core/ribbonicons.h).
constexpr int kStatusDotSize = 8;

// Small hand-drawn glyphs identifying the widget kind in the cell header —
// same "draw it, don't fake it" approach as arrowImagePath()/checkImagePath()
// in stylesheet.cpp, just painted live instead of cached to a QSS pixmap
// since this is a normal paintEvent, not a style sheet subcontrol. Silently
// draws nothing for any typeId without a glyph below (headerless control
// kinds never reach this — see wantsCellHeader()).
void drawTypeIcon(QPainter& painter, const QRect& r, const QString& typeId, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.topLeft());
    const qreal s = r.width(); // square icon box

    if (typeId == "dummy_line") {
        QPen pen(color, 1.5);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawPolyline(QPolygonF({QPointF(s * 0.05, s * 0.75), QPointF(s * 0.32, s * 0.45),
                                         QPointF(s * 0.55, s * 0.6), QPointF(s * 0.78, s * 0.2),
                                         QPointF(s * 0.95, s * 0.35)}));
    } else if (typeId == "dummy_bar") {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(QRectF(s * 0.08, s * 0.55, s * 0.22, s * 0.4));
        painter.drawRect(QRectF(s * 0.39, s * 0.25, s * 0.22, s * 0.7));
        painter.drawRect(QRectF(s * 0.7, s * 0.05, s * 0.22, s * 0.9));
    } else if (typeId == "dummy_gauge") {
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF arcRect(s * 0.08, s * 0.08, s * 0.84, s * 0.84);
        painter.drawArc(arcRect, 30 * 16, 300 * 16);
        painter.drawLine(QPointF(s * 0.5, s * 0.5), QPointF(s * 0.78, s * 0.3));
    } else if (typeId == "serial_monitor") {
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawPolyline(
            QPolygonF({QPointF(s * 0.1, s * 0.25), QPointF(s * 0.4, s * 0.5), QPointF(s * 0.1, s * 0.75)}));
        painter.drawLine(QPointF(s * 0.5, s * 0.82), QPointF(s * 0.9, s * 0.82));
    }

    painter.restore();
}

// Header-controls glyphs (see kStatusDotSize above) -- same "draw it, don't
// fake it" live-QPainter approach as drawTypeIcon(), deliberately not routed
// through the ribbon's QIcon/QPixmap factories (core/ribbonicons.cpp): those
// are fixed to the ribbon's own 16px/26px bordered-button scale, whereas
// these sit borderless inside the 24px cell header at kIconSize (14px).

// Play glyph (triangle) while paused -- click resumes; pause glyph (two
// bars) while running -- click pauses.
void drawPlayPauseIcon(QPainter& painter, const QRect& r, bool paused, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.topLeft());
    const qreal s = r.width();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    if (paused) {
        painter.drawPolygon(
            QPolygonF({QPointF(s * 0.22, s * 0.15), QPointF(s * 0.22, s * 0.85), QPointF(s * 0.85, s * 0.5)}));
    } else {
        const qreal barWidth = s * 0.22;
        painter.drawRect(QRectF(s * 0.2, s * 0.15, barWidth, s * 0.7));
        painter.drawRect(QRectF(s * 0.58, s * 0.15, barWidth, s * 0.7));
    }
    painter.restore();
}

// Stop/clear glyph -- the classic plain filled square.
void drawClearIcon(QPainter& painter, const QRect& r, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.topLeft());
    const qreal s = r.width();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRect(QRectF(s * 0.18, s * 0.18, s * 0.64, s * 0.64));
    painter.restore();
}

// Settings gear glyph -- a ringed hub with radial ticks standing in for
// teeth, legible at 14px without trying to render a literal gear silhouette.
void drawGearIcon(QPainter& painter, const QRect& r, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.center());
    const qreal s = r.width();
    const qreal bodyRadius = s * 0.28;
    const qreal toothOuterRadius = s * 0.44;

    QPen toothPen(color, s * 0.12);
    toothPen.setCapStyle(Qt::FlatCap);
    painter.setPen(toothPen);
    constexpr int kTeeth = 6;
    for (int i = 0; i < kTeeth; ++i) {
        painter.save();
        painter.rotate(360.0 / kTeeth * i);
        painter.drawLine(QPointF(0, -bodyRadius), QPointF(0, -toothOuterRadius));
        painter.restore();
    }

    painter.setPen(QPen(color, 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(0, 0), bodyRadius, bodyRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(0, 0), bodyRadius * 0.32, bodyRadius * 0.32);
    painter.restore();
}

// A parent's paintEvent always runs before its children are composed. The
// outline therefore cannot live in DashboardCell::paintEvent: an opaque
// DashboardWidget would cover its straight runs, while the rounded mask left
// a few isolated outline pixels visible at the corners. Keeping only the
// outline in this transparent, mouse-inert child makes it the last layer in
// the stack, so the same continuous stroke is visible on every edge.
class BorderOverlay final : public QWidget {
public:
    BorderOverlay(QVariantAnimation* selectionAnimation, DashboardWidget* content, QWidget* parent)
        : QWidget(parent), m_selectionAnimation(selectionAnimation), m_content(content) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this](const ThemePalette&) { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        const qreal selectT = m_selectionAnimation->currentValue().isValid()
                                  ? m_selectionAnimation->currentValue().toReal()
                                  : 0.0;
        const bool selectionVisible = selectT > 0.0;
        constexpr qreal kEdgeFinishWidth = 2.0;

        const QRectF borderRect = QRectF(rect()).adjusted(kEdgeFinishWidth / 2.0, kEdgeFinishWidth / 2.0,
                                                           -kEdgeFinishWidth / 2.0, -kEdgeFinishWidth / 2.0);
        const QPainterPath outline =
            partiallyRoundedRect(borderRect, kContainerCornerRadius, true, true, true, true);
        painter.setPen(QPen(m_content->cellFillColor(palette), kEdgeFinishWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);

        // In Layout, a headered cell has surfaceAlt at its top edge and the
        // content fill below it. Finish that segment in its own color too;
        // selection uses one accent outline around the entire cell instead.
        const int headerHeight = property("headerHeight").toInt();
        if (!selectionVisible && headerHeight > 0) {
            painter.save();
            painter.setClipRect(QRect(0, 0, width(), headerHeight));
            painter.setPen(QPen(palette.surfaceAlt, kEdgeFinishWidth));
            painter.drawPath(outline);
            painter.restore();
        }

        if (selectionVisible) {
            QColor selectionColor = property("dragInvalid").toBool() ? palette.danger : palette.accent;
            selectionColor.setAlphaF(selectT);
            painter.setPen(QPen(selectionColor, kEdgeFinishWidth));
            painter.drawPath(outline);
        }
    }

private:
    QVariantAnimation* m_selectionAnimation;
    DashboardWidget* m_content;
};
} // namespace

DashboardCell::DashboardCell(const QString& itemId, const QString& typeId, const QString& title,
                              DashboardWidget* content, QWidget* parent)
    : QWidget(parent), m_itemId(itemId), m_typeId(typeId), m_title(title), m_content(content) {
    // Opt out of the app-wide QWidget background. This wrapper must remain
    // transparent outside the rounded silhouette so the layout grid (dots
    // included) shows through its four corner notches.
    setProperty("dashboardCell", true);
    m_content->setParent(this);
    m_borderOverlay = new BorderOverlay(&m_selectionAnim, m_content, this);

    setMouseTracking(true);
    layoutChildren();

    m_selectionAnim.setDuration(kSelectionAnimMs);
    m_selectionAnim.setStartValue(0.0);
    m_selectionAnim.setEndValue(1.0);
    connect(&m_selectionAnim, &QVariantAnimation::valueChanged, m_borderOverlay,
            QOverload<>::of(&QWidget::update));
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
        m_selectionAnim.setDirection(QAbstractAnimation::Backward);
        m_selectionAnim.start();
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
    m_selectionAnim.setDirection(selected ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
    m_selectionAnim.start();
    updateCursor();
    update();
}

void DashboardCell::setConnected(bool connected) {
    if (m_connected == connected) {
        return;
    }
    m_connected = connected;
    update();
}

void DashboardCell::setDragInvalid(bool invalid) {
    if (m_dragInvalid == invalid) {
        return;
    }
    m_dragInvalid = invalid;
    m_borderOverlay->setProperty("dragInvalid", invalid);
    m_borderOverlay->update();
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

QRect DashboardCell::gearButtonRect() const {
    const QRect header = headerRect();
    const int y = (header.height() - kIconSize) / 2;
    return QRect(header.right() - kIconMargin - kIconSize + 1, y, kIconSize, kIconSize);
}

QRect DashboardCell::clearButtonRect() const {
    const QRect gear = gearButtonRect();
    return QRect(gear.left() - kIconMargin - kIconSize, gear.top(), kIconSize, kIconSize);
}

QRect DashboardCell::pauseButtonRect() const {
    const QRect clear = clearButtonRect();
    return QRect(clear.left() - kIconMargin - kIconSize, clear.top(), kIconSize, kIconSize);
}

void DashboardCell::showSettingsMenu() {
    // Kinds with nothing to configure yet (DummyBarChartWidget, see
    // DashboardWidget::hasChartOptionsMenu()) skip the menu entirely rather
    // than popping up an empty one.
    if (!m_content->hasChartOptionsMenu()) {
        return;
    }

    QMenu menu(this);
    QAction* lastValueAction = menu.addAction(tr("Show last value"));
    lastValueAction->setCheckable(true);
    lastValueAction->setChecked(m_content->showsLastValueRow());
    connect(lastValueAction, &QAction::toggled, this, [this](bool on) { m_content->setShowsLastValueRow(on); });

    QAction* gridPointsAction = menu.addAction(tr("Show grid point values"));
    gridPointsAction->setCheckable(true);
    gridPointsAction->setChecked(m_content->showsGridPointMarkers());
    connect(gridPointsAction, &QAction::toggled, this, [this](bool on) { m_content->setShowsGridPointMarkers(on); });

    QAction* hoverCrosshairAction = menu.addAction(tr("Show hover crosshair"));
    hoverCrosshairAction->setCheckable(true);
    hoverCrosshairAction->setChecked(m_content->showsHoverCrosshair());
    connect(hoverCrosshairAction, &QAction::toggled, this, [this](bool on) { m_content->setShowsHoverCrosshair(on); });

    menu.addSeparator();

    // Select box for the line-shape a line chart reconstructs between
    // buffered samples with (DashboardWidget::lineInterpolation(), see
    // ChartLineInterpolation in widgets/chartdata.h) -- a QComboBox via
    // QWidgetAction rather than a submenu of checkable actions, since the
    // user asked for a dropdown specifically. Covered by the
    // hasChartOptionsMenu() guard above like everything else in this menu, so
    // it only ever shows for DummyLineChartWidget in practice.
    auto* interpolationRow = new QWidget(&menu);
    auto* interpolationLayout = new QHBoxLayout(interpolationRow);
    interpolationLayout->setContentsMargins(12, 4, 12, 4);
    interpolationLayout->addWidget(new QLabel(tr("Interpolation:"), interpolationRow));
    auto* interpolationCombo = new QComboBox(interpolationRow);
    interpolationCombo->addItem(tr("Linear"), "linear");
    interpolationCombo->addItem(tr("ZOH (step)"), "zoh");
    interpolationCombo->addItem(tr("Stem"), "stem");
    interpolationCombo->addItem(tr("None (points)"), "none");
    interpolationCombo->setCurrentIndex(qMax(0, interpolationCombo->findData(m_content->lineInterpolation())));
    interpolationLayout->addWidget(interpolationCombo);
    connect(interpolationCombo, &QComboBox::currentIndexChanged, this, [this, interpolationCombo](int index) {
        m_content->setLineInterpolation(interpolationCombo->itemData(index).toString());
    });

    auto* interpolationAction = new QWidgetAction(&menu);
    interpolationAction->setDefaultWidget(interpolationRow);
    menu.addAction(interpolationAction);

    menu.exec(mapToGlobal(gearButtonRect().bottomLeft()));
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
    const int headerH = headerHeight();
    m_content->setGeometry(0, headerH, width(), height() - headerH);
    updateContentMask();
    m_borderOverlay->setProperty("headerHeight", headerH);
    m_borderOverlay->setGeometry(rect());
    m_borderOverlay->raise();
}

void DashboardCell::updateContentMask() {
    // Clips the content widget's own opaque background (WA_StyledBackground,
    // see dashboardwidget.h) to the same rounded silhouette as the border/
    // header this cell paints around it — otherwise the child's square
    // corners show through past the rounded outline. See "Corner radius" in
    // docs/VISUAL_IDENTITY.md. Only the corners that actually sit on the
    // cell's outer edge get rounded; a corner tucked under the header is
    // left square since it meets a straight internal seam, not the outline.
    const QRect local(QPoint(0, 0), m_content->size());
    if (local.isEmpty()) {
        return;
    }
    const bool headerPresent = headerHeight() > 0;
    m_content->setRoundedCorners(!headerPresent, !headerPresent, true, true);
    const QPainterPath path = m_content->contentFillPath();
    m_content->setMask(QRegion(path.toFillPolygon().toPolygon()));
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
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    // Paint only the cell silhouette. Filling rect() here erases the grid
    // backdrop in the four corner notches, which reads as a small square
    // around an otherwise rounded widget while editing the layout.
    painter.fillPath(partiallyRoundedRect(QRectF(rect()), kContainerCornerRadius, true, true, true, true),
                     palette.background);

    if (headerHeight() > 0) {
        painter.save();
        painter.setClipPath(partiallyRoundedRect(rect(), kContainerCornerRadius, true, true, true, true));
        painter.fillRect(headerRect(), m_selected ? palette.accent : palette.surfaceAlt);
        painter.restore();

        const QColor headerFg = m_selected ? palette.background : palette.textPrimary;
        QRect textRect = headerRect().adjusted(kIconMargin, 0, -kIconMargin, 0);

        const QRect iconRect(kIconMargin, (headerHeight() - kIconSize) / 2, kIconSize, kIconSize);
        drawTypeIcon(painter, iconRect, m_typeId, headerFg);
        if (iconRect.width() > 0) {
            textRect.setLeft(iconRect.right() + kIconMargin);
        }

        if (m_content->wantsHeaderControls()) {
            // Connection dot -- red/green, driven by setConnected() -- right
            // after the type glyph, ahead of the title.
            const QRect dotRect(textRect.left(), (headerHeight() - kStatusDotSize) / 2, kStatusDotSize,
                                 kStatusDotSize);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_connected ? palette.success : palette.danger);
            painter.drawEllipse(dotRect);
            textRect.setLeft(dotRect.right() + kIconMargin);

            // Right-aligned pause/resume + clear + gear -- textRect stops
            // short of them so a long title elides instead of running
            // underneath.
            textRect.setRight(pauseButtonRect().left() - kIconMargin);
            drawPlayPauseIcon(painter, pauseButtonRect(), m_content->isPaused(), headerFg);
            drawClearIcon(painter, clearButtonRect(), headerFg);
            drawGearIcon(painter, gearButtonRect(), headerFg);
        }

        painter.setPen(headerFg);
        const QFontMetrics fm(painter.font());
        const QString elidedTitle = fm.elidedText(m_title, Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);
    }

    if (!m_editMode || !m_selected) {
        // The grip is edit-only: nothing to resize outside Layout, and
        // unselected cells are identifiable but not interactive.
        return;
    }

    // Grip drawn as a small staircase of dots, echoing the DashboardGrid
    // background dots (see kGridDotRadius in dashboardgrid.cpp) instead of
    // the plain diagonal lines this used to be.
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.textSecondary);
    const QRect grip = gripRect();
    constexpr qreal kDotRadius = 1.3;
    constexpr int kStep = 4;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col <= row; ++col) {
            const qreal x = grip.right() - row * kStep;
            const qreal y = grip.bottom() - col * kStep;
            painter.drawEllipse(QPointF(x, y), kDotRadius, kDotRadius);
        }
    }
}

void DashboardCell::mousePressEvent(QMouseEvent* event) {
    // Header controls (pause/resume, clear, gear) are Run-mode-only -- in
    // Layout/edit mode the header stays a pure drag handle, unchanged from
    // before this feature.
    if (!m_editMode && event->button() == Qt::LeftButton && m_content->wantsHeaderControls() &&
        headerHeight() > 0 && headerRect().contains(event->position().toPoint())) {
        const QPoint pos = event->position().toPoint();
        if (pauseButtonRect().contains(pos)) {
            m_content->setPaused(!m_content->isPaused());
            update();
            event->accept();
            return;
        }
        if (clearButtonRect().contains(pos)) {
            m_content->clearChartData();
            event->accept();
            return;
        }
        if (gearButtonRect().contains(pos)) {
            showSettingsMenu();
            event->accept();
            return;
        }
    }

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
