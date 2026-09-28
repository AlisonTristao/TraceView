#include "dashboardcell.h"

#include <QAction>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <functional>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegion>
#include <QSignalBlocker>
#include <QWidgetAction>

#include "traceview/framestyle.h"
#include "dashboardwidget.h"
#include "theme/iconutils.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kGripSize = 14;   // hit/visual size of the 4 corner handles
constexpr int kEdgeMargin = 6;  // hit thickness of the 4 edge handles
constexpr int kSelectionAnimMs = 150;

constexpr int kIconSize = 14;
constexpr int kIconMargin = 6;
// Header-controls cluster (see DashboardWidget::headerControls()): the
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
    const qreal s = r.width();  // square icon box

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
        painter.drawPolyline(QPolygonF(
            {QPointF(s * 0.1, s * 0.25), QPointF(s * 0.4, s * 0.5), QPointF(s * 0.1, s * 0.75)}));
        painter.drawLine(QPointF(s * 0.5, s * 0.82), QPointF(s * 0.9, s * 0.82));
    } else if (typeId == "robot_log") {
        // Log sheet: a page outline with three text lines.
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(s * 0.15, s * 0.05, s * 0.7, s * 0.9), s * 0.1, s * 0.1);
        painter.drawLine(QPointF(s * 0.32, s * 0.3), QPointF(s * 0.68, s * 0.3));
        painter.drawLine(QPointF(s * 0.32, s * 0.5), QPointF(s * 0.68, s * 0.5));
        painter.drawLine(QPointF(s * 0.32, s * 0.7), QPointF(s * 0.55, s * 0.7));
    } else if (typeId == "chat") {
        // Speech bubble with a tail at the bottom left.
        QPen pen(color, 1.5);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(s * 0.08, s * 0.1, s * 0.84, s * 0.6), s * 0.15, s * 0.15);
        painter.drawPolyline(QPolygonF(
            {QPointF(s * 0.3, s * 0.7), QPointF(s * 0.2, s * 0.92), QPointF(s * 0.48, s * 0.7)}));
    }

    painter.restore();
}

// Header-controls glyphs (see kStatusDotSize above) -- same "draw it, don't
// fake it" live-QPainter approach as drawTypeIcon(), deliberately not routed
// through the ribbon's QIcon/QPixmap factories (core/ribbonicons.cpp): those
// are fixed to the ribbon's own 16px/26px bordered-button scale, whereas
// these sit borderless inside the 24px cell header at kIconSize (14px).

// Play glyph (triangle) while paused -- click resumes; pause glyph (two
// bars) while running -- click pauses. Loaded from resources/icons/dashboard/
// (see theme/iconutils.h) instead of hand-drawn, same as the ribbon icons.
void drawPlayPauseIcon(QPainter& painter, const QRect& r, bool paused, const QColor& color) {
    drawTintedIcon(painter, r, paused ? ":/icons/dashboard/play.svg" : ":/icons/dashboard/pause.svg",
                   color);
}

// Stop/clear glyph -- the classic plain filled square.
void drawClearIcon(QPainter& painter, const QRect& r, const QColor& color) {
    drawTintedIcon(painter, r, ":/icons/dashboard/clear.svg", color);
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
        const FrameStyle& frame = ThemeManager::instance().currentFrameStyle();

        // One path for both outlines, inset by the (wider) selection stroke
        // so neither is clipped at the widget edge.
        constexpr qreal kInset = kFrameSelectionWidth / 2.0;
        const QRectF borderRect = QRectF(rect()).adjusted(kInset, kInset, -kInset, -kInset);
        const QPainterPath outline = frameShapePath(borderRect, frame);

        // Same palette.border convention as DeviceCard's outline (see
        // devicecard.cpp): always visible, so a cell's extent reads clearly
        // whether or not it's selected. Selection layers a distinct accent
        // outline on top instead of this stroke appearing/disappearing.
        // Skipped for headerless controls (push button/toggle/slider,
        // widgets/controlwidgets.cpp) -- those already read as bare controls
        // rather than cards (see "Control panel fill" in
        // docs/VISUAL_IDENTITY.md), and an idle outline would fight that.
        // The frame style may drop the idle outline entirely (Borderless).
        if (m_content->wantsCellHeader() && frame.idleOutline && frame.borderWidth > 0.0) {
            painter.setPen(QPen(palette.border, frame.borderWidth));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(outline);
        }

        if (selectionVisible) {
            QColor selectionColor =
                property("dragInvalid").toBool() ? palette.danger : palette.accent;
            selectionColor.setAlphaF(selectT);
            painter.setPen(QPen(selectionColor, kFrameSelectionWidth));
            painter.drawPath(outline);
        }
    }

private:
    QVariantAnimation* m_selectionAnimation;
    DashboardWidget* m_content;
};
// The "On hover" card header (CardHeaderId::Hover): the title strip drawn
// over the top of the content while the mouse is over the card, instead of
// taking space above it. Paints and handles clicks through the owning cell,
// so it is the exact same strip with the exact same buttons.
class HoverHeaderStrip final : public QWidget {
public:
    HoverHeaderStrip(std::function<void(QPainter&)> paint,
                     std::function<bool(const QPoint&)> press, QWidget* parent)
        : QWidget(parent), m_paint(std::move(paint)), m_press(std::move(press)) {
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_paint(painter);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_press(event->position().toPoint())) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    std::function<void(QPainter&)> m_paint;
    std::function<bool(const QPoint&)> m_press;
};
}  // namespace

DashboardCell::DashboardCell(const QString& itemId, const QString& typeId, const QString& title,
                             DashboardWidget* content, QWidget* parent)
    : QWidget(parent), m_itemId(itemId), m_typeId(typeId), m_title(title), m_content(content) {
    // Opt out of the app-wide QWidget background. This wrapper must remain
    // transparent outside the rounded silhouette so the layout grid (dots
    // included) shows through its four corner notches.
    setProperty("dashboardCell", true);
    m_content->setParent(this);
    m_borderOverlay = new BorderOverlay(&m_selectionAnim, m_content, this);
    m_hoverHeader = new HoverHeaderStrip(
        [this](QPainter& painter) { paintHeader(painter, /*floating=*/true); },
        [this](const QPoint& pos) { return handleHeaderPress(pos); }, this);
    m_hoverHeader->hide();

    setMouseTracking(true);
    layoutChildren();

    m_selectionAnim.setDuration(kSelectionAnimMs);
    m_selectionAnim.setStartValue(0.0);
    m_selectionAnim.setEndValue(1.0);
    connect(&m_selectionAnim, &QVariantAnimation::valueChanged, m_borderOverlay,
            QOverload<>::of(&QWidget::update));
    // Any appearance change arrives as themeChanged: a frame style reshapes
    // the silhouette (content mask), a density changes the header height,
    // a card header style may hide the strip -- so lay out again, not just
    // repaint.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) {
                layoutChildren();
                update();
            });
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
    m_content->setEditModeHint(enabled);
    if (!enabled) {
        m_selected = false;
        startSelectionAnimation(QAbstractAnimation::Backward);
    }
    m_hoverHeader->hide();
    updateCursor();
    layoutChildren();
    update();
}

void DashboardCell::setSelected(bool selected) {
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    startSelectionAnimation(selected ? QAbstractAnimation::Forward
                                     : QAbstractAnimation::Backward);
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

void DashboardCell::setResizable(bool resizable) {
    if (m_resizable == resizable) {
        return;
    }
    m_resizable = resizable;
    update();
}

void DashboardCell::startSelectionAnimation(QAbstractAnimation::Direction direction) {
    // Reduced motion: the outline snaps instead of fading.
    m_selectionAnim.setDuration(ThemeManager::instance().reduceMotion() ? 0 : kSelectionAnimMs);
    m_selectionAnim.setDirection(direction);
    m_selectionAnim.start();
}

int DashboardCell::headerStripHeight() const {
    return m_content->wantsCellHeader() ? ThemeManager::instance().currentDensity().headerHeight
                                        : 0;
}

int DashboardCell::headerHeight() const {
    if (!m_editMode && ThemeManager::instance().cardHeader() == CardHeaderId::Hover) {
        return 0;
    }
    return headerStripHeight();
}

QRect DashboardCell::headerRect() const {
    return QRect(0, 0, width(), headerStripHeight());
}

QRect DashboardCell::gripRect() const {
    return QRect(width() - kGripSize, height() - kGripSize, kGripSize, kGripSize);
}

QRect DashboardCell::headerButtonRect(DashboardWidget::HeaderControl control) const {
    using HC = DashboardWidget::HeaderControl;
    const DashboardWidget::HeaderControls offered = m_content->headerControls();
    if (!offered.testFlag(control) || control == HC::ConnectionDot) {
        return QRect();
    }
    const QRect header = headerRect();
    const int y = (header.height() - kIconSize) / 2;
    int right = header.right() + 1 - kIconMargin;
    for (HC button : {HC::Settings, HC::Clear, HC::Pause}) {
        if (!offered.testFlag(button)) {
            continue;
        }
        const QRect rect(right - kIconSize, y, kIconSize, kIconSize);
        if (button == control) {
            return rect;
        }
        right = rect.left() - kIconMargin;
    }
    return QRect();
}

namespace {

// Flipping a checkable entry keeps the gear menu open, so several options
// can be changed in one visit -- a plain QMenu closes on every triggered
// action. Select boxes already keep it open (their popup is its own window).
class StickyMenu : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (triggerCheckable()) {
            event->accept();
            return;
        }
        QMenu::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        const bool activates = event->key() == Qt::Key_Return ||
                               event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space;
        if (activates && triggerCheckable()) {
            event->accept();
            return;
        }
        QMenu::keyPressEvent(event);
    }

private:
    bool triggerCheckable() {
        QAction* action = activeAction();
        if (!action || !action->isEnabled() || !action->isCheckable()) {
            return false;
        }
        action->trigger();
        return true;
    }
};

}  // namespace

void DashboardCell::showSettingsMenu() {
    // Kinds with nothing to configure skip the menu entirely rather than
    // popping up an empty one.
    const QVector<WidgetViewOption> options = m_content->viewOptions();
    if (options.isEmpty()) {
        return;
    }

    StickyMenu menu(this);
    QHash<QString, QAction*> toggles;
    QHash<QString, QComboBox*> combos;
    QHash<QString, QVector<QAction*>> multiChoices;

    // Re-reads every control from the widget after each change, since the
    // menu stays open across changes.
    auto resync = [&]() {
        for (const WidgetViewOption& option : m_content->viewOptions()) {
            if (QAction* action = toggles.value(option.id)) {
                const QSignalBlocker blocker(action);
                action->setChecked(option.value.toBool());
            }
            if (QComboBox* combo = combos.value(option.id)) {
                const QSignalBlocker blocker(combo);
                combo->setCurrentIndex(qMax(0, combo->findData(option.value.toString())));
            }
            const QStringList checked = option.value.toStringList();
            for (QAction* action : multiChoices.value(option.id)) {
                const QSignalBlocker blocker(action);
                action->setChecked(checked.contains(action->data().toString()));
            }
        }
    };
    auto apply = [this, &resync](const QString& id, const QVariant& value) {
        const QJsonObject view = m_content->viewConfigWith(id, value);
        m_content->setViewConfig(view);
        emit viewConfigChanged(m_itemId, view);
        resync();
    };

    for (int i = 0; i < options.size(); ++i) {
        const WidgetViewOption& option = options[i];
        if (option.startsSection && i > 0) {
            menu.addSeparator();
        }
        if (option.kind == WidgetViewOption::Kind::Toggle) {
            QAction* action = menu.addAction(option.label);
            action->setCheckable(true);
            action->setChecked(option.value.toBool());
            const QString id = option.id;
            connect(action, &QAction::toggled, this, [apply, id](bool on) { apply(id, on); });
            toggles.insert(option.id, action);
            continue;
        }
        if (option.kind == WidgetViewOption::Kind::MultiChoice) {
            // A sticky submenu: several choices can be flipped in one visit.
            auto* submenu = new StickyMenu(option.label, &menu);
            menu.addMenu(submenu);
            const QString id = option.id;
            QVector<QAction*> actions;
            for (const auto& choice : option.choices) {
                QAction* action = submenu->addAction(choice.second);
                action->setCheckable(true);
                action->setData(choice.first);
                action->setChecked(option.value.toStringList().contains(choice.first));
                actions.append(action);
            }
            for (QAction* action : actions) {
                connect(action, &QAction::toggled, this, [apply, actions, id](bool) {
                    QStringList checked;
                    for (QAction* each : actions) {
                        if (each->isChecked()) {
                            checked << each->data().toString();
                        }
                    }
                    apply(id, checked);
                });
            }
            multiChoices.insert(option.id, actions);
            continue;
        }

        // A select box via QWidgetAction rather than a submenu of checkable
        // actions -- the user asked for a dropdown specifically.
        auto* row = new QWidget(&menu);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 4, 12, 4);
        rowLayout->addWidget(new QLabel(option.label, row));
        auto* combo = new QComboBox(row);
        for (const auto& choice : option.choices) {
            combo->addItem(choice.second, choice.first);
        }
        combo->setCurrentIndex(qMax(0, combo->findData(option.value.toString())));
        rowLayout->addWidget(combo);
        const QString id = option.id;
        connect(combo, &QComboBox::currentIndexChanged, this,
                [apply, combo, id](int index) { apply(id, combo->itemData(index)); });
        combos.insert(option.id, combo);

        auto* widgetAction = new QWidgetAction(&menu);
        widgetAction->setDefaultWidget(row);
        menu.addAction(widgetAction);
    }

    menu.exec(mapToGlobal(
        headerButtonRect(DashboardWidget::HeaderControl::Settings).bottomLeft()));
}

DashboardCell::ResizeHandle DashboardCell::handleAt(const QPoint& pos) const {
    if (!m_resizable) {
        return ResizeHandle::None;
    }
    const bool nearLeft = pos.x() <= kGripSize;
    const bool nearRight = pos.x() >= width() - kGripSize;
    const bool nearTop = pos.y() <= kGripSize;
    const bool nearBottom = pos.y() >= height() - kGripSize;

    // Corners (bigger squares) take priority over the thinner edge bands.
    if (nearTop && nearLeft)
        return ResizeHandle::TopLeft;
    if (nearTop && nearRight)
        return ResizeHandle::TopRight;
    if (nearBottom && nearLeft)
        return ResizeHandle::BottomLeft;
    if (nearBottom && nearRight)
        return ResizeHandle::BottomRight;

    if (pos.y() <= kEdgeMargin)
        return ResizeHandle::Top;
    if (pos.y() >= height() - kEdgeMargin)
        return ResizeHandle::Bottom;
    if (pos.x() <= kEdgeMargin)
        return ResizeHandle::Left;
    if (pos.x() >= width() - kEdgeMargin)
        return ResizeHandle::Right;

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
    m_hoverHeader->setGeometry(headerRect());
    if (headerH > 0) {
        m_hoverHeader->hide();
    }
    m_hoverHeader->raise();
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
        setCursor(Qt::PointingHandCursor);  // click to select
    } else {
        unsetCursor();  // selected: hover logic below drives it; not editing: default
    }
}

void DashboardCell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutChildren();
}

void DashboardCell::paintHeader(QPainter& painter, bool floating) const {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const CardHeaderId style = ThemeManager::instance().cardHeader();
    const QRect header = headerRect();
    const int stripHeight = header.height();

    // Filled (and the floating hover strip): a solid strip, accent while
    // selected. Line: the card's own fill with a separator underneath, the
    // accent moving to the title and separator while selected.
    const bool line = style == CardHeaderId::Line && !floating;
    painter.save();
    painter.setClipPath(currentFramePath(QRectF(rect())));
    if (line) {
        painter.fillRect(header, palette.surface);
        painter.setPen(QPen(m_selected ? palette.accent : palette.border, m_selected ? 2 : 1));
        const qreal y = header.bottom() + (m_selected ? 0.0 : 0.5);
        painter.drawLine(QPointF(header.left(), y), QPointF(header.right() + 1, y));
    } else {
        QColor fill = m_selected ? palette.accent : palette.surfaceAlt;
        if (floating) {
            fill.setAlphaF(0.94f);
        }
        painter.fillRect(header, fill);
    }
    painter.restore();

    const QColor headerFg = line ? (m_selected ? palette.accent : palette.textPrimary)
                                 : (m_selected ? palette.background : palette.textPrimary);
    QRect textRect = header.adjusted(kIconMargin, 0, -kIconMargin, 0);

    const QRect iconRect(kIconMargin, (stripHeight - kIconSize) / 2, kIconSize, kIconSize);
    drawTypeIcon(painter, iconRect, m_typeId, headerFg);
    if (iconRect.width() > 0) {
        textRect.setLeft(iconRect.right() + kIconMargin);
    }

    using HC = DashboardWidget::HeaderControl;
    const DashboardWidget::HeaderControls controls = m_content->headerControls();
    if (controls.testFlag(HC::ConnectionDot)) {
        // Connection dot -- red/green, driven by setConnected() -- right
        // after the type glyph, ahead of the title.
        const QRect dotRect(textRect.left(), (stripHeight - kStatusDotSize) / 2, kStatusDotSize,
                            kStatusDotSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_connected ? palette.success : palette.danger);
        painter.drawEllipse(dotRect);
        textRect.setLeft(dotRect.right() + kIconMargin);
    }

    // Right-aligned buttons -- textRect stops short of the leftmost one so a
    // long title elides instead of running underneath.
    for (HC button : {HC::Settings, HC::Clear, HC::Pause}) {
        const QRect rect = headerButtonRect(button);
        if (rect.isNull()) {
            continue;
        }
        textRect.setRight(qMin(textRect.right(), rect.left() - kIconMargin));
        if (button == HC::Settings) {
            drawGearIcon(painter, rect, headerFg);
        } else if (button == HC::Clear) {
            drawClearIcon(painter, rect, headerFg);
        } else {
            drawPlayPauseIcon(painter, rect, m_content->isPaused(), headerFg);
        }
    }

    painter.setPen(headerFg);
    const QFontMetrics fm(painter.font());
    const QString elidedTitle = fm.elidedText(m_title, Qt::ElideRight, textRect.width());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);
}

bool DashboardCell::handleHeaderPress(const QPoint& pos) {
    using HC = DashboardWidget::HeaderControl;
    if (m_editMode) {
        return false;
    }
    if (headerButtonRect(HC::Pause).contains(pos)) {
        m_content->setPaused(!m_content->isPaused());
        update();
        m_hoverHeader->update();
        return true;
    }
    if (headerButtonRect(HC::Clear).contains(pos)) {
        m_content->clearChartData();
        emit chartDataCleared(m_content);
        return true;
    }
    if (headerButtonRect(HC::Settings).contains(pos)) {
        showSettingsMenu();
        return true;
    }
    return false;
}

void DashboardCell::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    // Paint only the cell silhouette. Filling rect() here erases the grid
    // backdrop in the four corner notches, which reads as a small square
    // around an otherwise rounded widget while editing the layout.
    const QPainterPath silhouette = currentFramePath(QRectF(rect()));
    painter.fillPath(silhouette, palette.background);

    if (headerHeight() > 0) {
        paintHeader(painter, /*floating=*/false);
    }

    if (!m_editMode || !m_selected || !m_resizable) {
        // The grip is edit-only: nothing to resize outside Layout, unselected
        // cells are identifiable but not interactive, and a multi-selection/
        // group can be moved but not resized (see setResizable()).
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
    if (!m_editMode && event->button() == Qt::LeftButton && headerHeight() > 0 &&
        headerRect().contains(event->position().toPoint()) &&
        handleHeaderPress(event->position().toPoint())) {
        event->accept();
        return;
    }

    if (!m_editMode || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // Ctrl-click always just toggles this cell's selection membership (or
    // its whole group's, see DashboardGrid::toggleItemSelection()) --
    // whether or not it's already selected -- and never starts a drag/
    // resize, unlike a plain click below.
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        emit selectRequested(m_itemId, event->modifiers());
        event->accept();
        return;
    }

    if (!m_selected) {
        emit selectRequested(m_itemId, event->modifiers());
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

void DashboardCell::enterEvent(QEnterEvent* event) {
    // "On hover" card header: the strip floats in over the content while the
    // mouse is over the card (Run mode only -- edit mode always reserves it).
    if (!m_editMode && headerHeight() == 0 && headerStripHeight() > 0) {
        m_hoverHeader->setGeometry(headerRect());
        m_hoverHeader->show();
        m_hoverHeader->raise();
        m_borderOverlay->raise();
    }
    QWidget::enterEvent(event);
}

void DashboardCell::leaveEvent(QEvent* event) {
    if (m_selected) {
        unsetCursor();
    }
    m_hoverHeader->hide();
    QWidget::leaveEvent(event);
}

}  // namespace traceview
