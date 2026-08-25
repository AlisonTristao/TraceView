#include "paneldockcontroller.h"

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWidget>
#include <algorithm>

#include "dockablepanel.h"
#include "dockablepanelheader.h"
#include "dockdropindicator.h"
#include "dockresizegrip.h"

namespace traceview {

namespace {
constexpr const char* kSettingsPrefix = "panelDock/";
// How close the cursor needs to be to contentRow's border, in pixels, before
// a drag snaps to that edge instead of leaving the panel floating.
constexpr int kEdgeSnapThresholdPx = 40;
constexpr int kDefaultFloatingHeight = 400;
// Never let a panel (docked or floating) shrink below this on its resizable
// axis/axes.
constexpr int kMinPanelThickness = 120;
// Reserved for the canvas to stay at least somewhat visible/usable when
// clamping how far a docked panel can grow.
constexpr int kMinCanvasVisible = 100;

QString edgeToString(DockEdge edge) {
    switch (edge) {
        case DockEdge::Left:
            return QStringLiteral("left");
        case DockEdge::Right:
            return QStringLiteral("right");
        case DockEdge::Top:
            return QStringLiteral("top");
        case DockEdge::Bottom:
            return QStringLiteral("bottom");
        case DockEdge::Floating:
            return QStringLiteral("floating");
    }
    return QStringLiteral("left");
}

DockEdge edgeFromString(const QString& text) {
    if (text == QStringLiteral("right")) {
        return DockEdge::Right;
    }
    if (text == QStringLiteral("top")) {
        return DockEdge::Top;
    }
    if (text == QStringLiteral("bottom")) {
        return DockEdge::Bottom;
    }
    if (text == QStringLiteral("floating")) {
        return DockEdge::Floating;
    }
    return DockEdge::Left;
}

bool isGeometryOnScreen(const QRect& geometry) {
    if (!geometry.isValid()) {
        return false;
    }
    for (const QScreen* screen : QGuiApplication::screens()) {
        if (screen->geometry().intersects(geometry)) {
            return true;
        }
    }
    return false;
}
}  // namespace

PanelDockController::PanelDockController(QWidget* contentRow, QWidget* window, QObject* parent)
    : QObject(parent), m_contentRow(contentRow), m_window(window) {
    m_indicator = new DockDropIndicator(m_contentRow);
}

void PanelDockController::registerPanel(DockablePanel* panel, const QString& settingsId,
                                        DockEdge defaultEdge) {
    PanelState state;
    state.edge = defaultEdge;
    state.settingsId = settingsId;
    state.thickness = panel->preferredThickness();
    state.edgeGrip = new DockResizeGrip(DockResizeGrip::Orientation::Horizontal, m_contentRow);
    state.cornerGrip = new DockResizeGrip(DockResizeGrip::Orientation::Corner, panel);
    m_states.insert(panel, state);
    m_dockOrder.append(panel);

    panel->installEventFilter(this);

    connect(panel->header(), &DockablePanelHeader::dragStarted, this,
            [this, panel](QPoint globalPos) { onDragStarted(panel, globalPos); });
    connect(panel->header(), &DockablePanelHeader::dragMoved, this,
            [this, panel](QPoint globalPos) { onDragMoved(panel, globalPos); });
    connect(panel->header(), &DockablePanelHeader::dragFinished, this,
            [this, panel](QPoint globalPos) { onDragFinished(panel, globalPos); });

    DockResizeGrip* edgeGrip = m_states[panel].edgeGrip;
    connect(edgeGrip, &DockResizeGrip::dragStarted, this,
            [this, panel](QPoint globalPos) { onEdgeResizeStarted(panel, globalPos); });
    connect(edgeGrip, &DockResizeGrip::dragMoved, this,
            [this, panel](QPoint globalPos) { onEdgeResizeMoved(panel, globalPos); });
    connect(edgeGrip, &DockResizeGrip::dragFinished, this,
            [this, panel](QPoint globalPos) { onEdgeResizeFinished(panel, globalPos); });

    DockResizeGrip* cornerGrip = m_states[panel].cornerGrip;
    connect(cornerGrip, &DockResizeGrip::dragStarted, this,
            [this, panel](QPoint globalPos) { onCornerResizeStarted(panel, globalPos); });
    connect(cornerGrip, &DockResizeGrip::dragMoved, this,
            [this, panel](QPoint globalPos) { onCornerResizeMoved(panel, globalPos); });
    connect(cornerGrip, &DockResizeGrip::dragFinished, this,
            [this, panel](QPoint globalPos) { onCornerResizeFinished(panel, globalPos); });
}

void PanelDockController::relayout() {
    for (DockablePanel* panel : m_dockOrder) {
        const PanelState state = m_states.value(panel);
        if (state.edge != DockEdge::Floating) {
            panel->setGeometry(geometryForEdge(state.edge, panel));
        }
        updateGripVisibility(panel);
    }
}

void PanelDockController::restoreState() {
    QSettings settings;
    for (DockablePanel* panel : m_dockOrder) {
        PanelState& state = m_states[panel];
        const QString prefix = QString(kSettingsPrefix) + state.settingsId + "/";
        if (!settings.contains(prefix + "edge")) {
            continue;  // keep the default passed to registerPanel()
        }
        if (settings.contains(prefix + "thickness")) {
            state.thickness =
                std::max(kMinPanelThickness, settings.value(prefix + "thickness").toInt());
        }

        const DockEdge edge = edgeFromString(settings.value(prefix + "edge").toString());
        if (edge == DockEdge::Floating) {
            QRect geometry = settings.value(prefix + "floatingGeometry").toRect();
            if (!isGeometryOnScreen(geometry)) {
                geometry = defaultFloatingGeometry(panel);
            }
            state.edge = DockEdge::Floating;
            panel->setParent(m_window, Qt::Tool | Qt::FramelessWindowHint);
            panel->setGeometry(geometry);
        } else {
            dockPanel(panel, edge);
        }
    }
    relayout();
}

void PanelDockController::onDragStarted(DockablePanel* panel, QPoint globalPos) {
    m_draggingPanel = panel;
    m_grabOffset = globalPos - panel->mapToGlobal(QPoint(0, 0));

    PanelState& state = m_states[panel];
    if (state.edge != DockEdge::Floating) {
        const QRect saved =
            QSettings()
                .value(QString(kSettingsPrefix) + state.settingsId + "/floatingGeometry")
                .toRect();
        const QSize size =
            saved.isValid() ? saved.size() : QSize(state.thickness, kDefaultFloatingHeight);
        state.edge = DockEdge::Floating;
        panel->setParent(m_window, Qt::Tool | Qt::FramelessWindowHint);
        panel->resize(size);
    }
    panel->move(globalPos - m_grabOffset);
    panel->show();
    panel->raise();
    updateGripVisibility(panel);
}

void PanelDockController::onDragMoved(DockablePanel* panel, QPoint globalPos) {
    if (panel != m_draggingPanel) {
        return;
    }
    panel->move(globalPos - m_grabOffset);

    m_candidateEdge = edgeForGlobalPos(globalPos);
    if (m_candidateEdge) {
        m_indicator->setGeometry(geometryForEdge(*m_candidateEdge, panel));
        m_indicator->show();
        m_indicator->raise();
    } else {
        m_indicator->hide();
    }
}

void PanelDockController::onDragFinished(DockablePanel* panel, QPoint globalPos) {
    if (panel != m_draggingPanel) {
        return;
    }
    panel->move(globalPos - m_grabOffset);
    m_indicator->hide();

    const std::optional<DockEdge> edge = edgeForGlobalPos(globalPos);
    if (edge) {
        dockPanel(panel, *edge);
        // dockPanel() doesn't show()/raise() (see its comment) -- but the
        // panel was visible throughout this drag, so restore that now that
        // reparenting has reset it to hidden.
        panel->show();
        panel->raise();
        updateGripVisibility(panel);
    } else {
        m_states[panel].edge = DockEdge::Floating;
        updateGripVisibility(panel);
    }
    saveState(panel);

    m_draggingPanel = nullptr;
    m_candidateEdge.reset();
    emit dragFinished();
}

void PanelDockController::onEdgeResizeStarted(DockablePanel* panel, QPoint globalPos) {
    m_resizingPanel = panel;
    m_resizingCorner = false;
    m_lastResizePos = globalPos;
}

void PanelDockController::onEdgeResizeMoved(DockablePanel* panel, QPoint globalPos) {
    if (panel != m_resizingPanel || m_resizingCorner) {
        return;
    }
    const QPoint delta = globalPos - m_lastResizePos;
    m_lastResizePos = globalPos;

    PanelState& state = m_states[panel];
    int deltaThickness = 0;
    switch (state.edge) {
        case DockEdge::Left:
            deltaThickness = delta.x();
            break;
        case DockEdge::Right:
            deltaThickness = -delta.x();
            break;
        case DockEdge::Top:
            deltaThickness = delta.y();
            break;
        case DockEdge::Bottom:
            deltaThickness = -delta.y();
            break;
        case DockEdge::Floating:
            return;
    }
    state.thickness = clampThickness(state.edge, panel, state.thickness + deltaThickness);
    relayout();
}

void PanelDockController::onEdgeResizeFinished(DockablePanel* panel, QPoint) {
    if (panel != m_resizingPanel) {
        return;
    }
    m_resizingPanel = nullptr;
    saveState(panel);
    emit dragFinished();
}

void PanelDockController::onCornerResizeStarted(DockablePanel* panel, QPoint globalPos) {
    m_resizingPanel = panel;
    m_resizingCorner = true;
    m_lastResizePos = globalPos;
}

void PanelDockController::onCornerResizeMoved(DockablePanel* panel, QPoint globalPos) {
    if (panel != m_resizingPanel || !m_resizingCorner) {
        return;
    }
    const QPoint delta = globalPos - m_lastResizePos;
    m_lastResizePos = globalPos;

    const QSize newSize(std::max(kMinPanelThickness, panel->width() + delta.x()),
                        std::max(kMinPanelThickness, panel->height() + delta.y()));
    panel->resize(newSize);
    updateCornerGripGeometry(panel);
}

void PanelDockController::onCornerResizeFinished(DockablePanel* panel, QPoint) {
    if (panel != m_resizingPanel) {
        return;
    }
    m_resizingPanel = nullptr;
    saveState(panel);
    emit dragFinished();
}

bool PanelDockController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        for (DockablePanel* panel : m_dockOrder) {
            if (static_cast<QObject*>(panel) == watched) {
                updateGripVisibility(panel);
                break;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

std::optional<DockEdge> PanelDockController::edgeForGlobalPos(QPoint globalPos) const {
    const QPoint local = m_contentRow->mapFromGlobal(globalPos);
    if (!m_contentRow->rect().contains(local)) {
        return std::nullopt;
    }

    const int left = local.x();
    const int right = m_contentRow->width() - local.x();
    const int top = local.y();
    const int bottom = m_contentRow->height() - local.y();
    const int margin = std::min({left, right, top, bottom});
    if (margin > kEdgeSnapThresholdPx) {
        return std::nullopt;
    }
    if (margin == left) {
        return DockEdge::Left;
    }
    if (margin == right) {
        return DockEdge::Right;
    }
    if (margin == top) {
        return DockEdge::Top;
    }
    return DockEdge::Bottom;
}

QRect PanelDockController::geometryForEdge(DockEdge edge, DockablePanel* panel) const {
    const int width = m_contentRow->width();
    const int height = m_contentRow->height();
    const int thickness = m_states.value(panel).thickness;

    // Panels registered earlier sit closer to the edge; a later one sharing
    // the same edge stacks further out, extending away from it.
    int offset = 0;
    for (DockablePanel* other : m_dockOrder) {
        if (other == panel) {
            break;
        }
        const PanelState otherState = m_states.value(other);
        if (otherState.edge == edge) {
            offset += otherState.thickness;
        }
    }

    switch (edge) {
        case DockEdge::Left:
            return QRect(offset, 0, thickness, height);
        case DockEdge::Right:
            return QRect(width - offset - thickness, 0, thickness, height);
        case DockEdge::Top:
            return QRect(0, offset, width, thickness);
        case DockEdge::Bottom:
            return QRect(0, height - offset - thickness, width, thickness);
        case DockEdge::Floating:
            break;
    }
    return QRect();
}

QRect PanelDockController::defaultFloatingGeometry(DockablePanel* panel) const {
    const QSize size(m_states.value(panel).thickness, kDefaultFloatingHeight);
    const QPoint center = m_window->geometry().center();
    return QRect(center - QPoint(size.width() / 2, size.height() / 2), size);
}

int PanelDockController::clampThickness(DockEdge edge, DockablePanel* panel, int desired) const {
    const bool horizontal = (edge == DockEdge::Left || edge == DockEdge::Right);
    const int totalDim = horizontal ? m_contentRow->width() : m_contentRow->height();

    int othersThickness = 0;
    for (DockablePanel* other : m_dockOrder) {
        if (other == panel) {
            continue;
        }
        const PanelState otherState = m_states.value(other);
        if (otherState.edge == edge) {
            othersThickness += otherState.thickness;
        }
    }

    const int maxThickness =
        std::max(kMinPanelThickness, totalDim - othersThickness - kMinCanvasVisible);
    return std::clamp(desired, kMinPanelThickness, maxThickness);
}

void PanelDockController::dockPanel(DockablePanel* panel, DockEdge edge) {
    // Deliberately doesn't show()/raise() -- reparenting hides the widget
    // (Qt resets visibility on setParent()), and this runs both from a live
    // drag (where the caller re-shows it, see onDragFinished()) and from
    // restoreState() at startup, where the panel must stay exactly as hidden
    // as buildLayersPanel()/buildPropertiesPanel() left it until
    // updatePanelVisibility() decides otherwise.
    PanelState& state = m_states[panel];
    state.edge = edge;
    panel->setParent(m_contentRow, Qt::Widget);
    relayout();
}

void PanelDockController::saveState(DockablePanel* panel) const {
    const PanelState state = m_states.value(panel);
    QSettings settings;
    const QString prefix = QString(kSettingsPrefix) + state.settingsId + "/";
    settings.setValue(prefix + "edge", edgeToString(state.edge));
    settings.setValue(prefix + "thickness", state.thickness);
    if (state.edge == DockEdge::Floating) {
        settings.setValue(prefix + "floatingGeometry", panel->geometry());
    }
}

void PanelDockController::updateGripVisibility(DockablePanel* panel) {
    const PanelState state = m_states.value(panel);
    const bool floating = state.edge == DockEdge::Floating;
    const bool visible = panel->isVisible();

    state.edgeGrip->setVisible(visible && !floating);
    if (visible && !floating) {
        updateEdgeGripGeometry(panel);
        state.edgeGrip->raise();
    }

    state.cornerGrip->setVisible(visible && floating);
    if (visible && floating) {
        updateCornerGripGeometry(panel);
        state.cornerGrip->raise();
    }
}

void PanelDockController::updateEdgeGripGeometry(DockablePanel* panel) {
    const PanelState state = m_states.value(panel);
    const bool horizontal = (state.edge == DockEdge::Left || state.edge == DockEdge::Right);
    state.edgeGrip->setOrientation(horizontal ? DockResizeGrip::Orientation::Horizontal
                                              : DockResizeGrip::Orientation::Vertical);

    const QRect r = panel->geometry();  // contentRow-local
    QRect gripRect;
    switch (state.edge) {
        case DockEdge::Left:
            gripRect = QRect(r.right() + 1, r.top(), kDockResizeGripThickness, r.height());
            break;
        case DockEdge::Right:
            gripRect = QRect(r.left() - kDockResizeGripThickness, r.top(), kDockResizeGripThickness,
                             r.height());
            break;
        case DockEdge::Top:
            gripRect = QRect(r.left(), r.bottom() + 1, r.width(), kDockResizeGripThickness);
            break;
        case DockEdge::Bottom:
            gripRect = QRect(r.left(), r.top() - kDockResizeGripThickness, r.width(),
                             kDockResizeGripThickness);
            break;
        case DockEdge::Floating:
            return;
    }
    state.edgeGrip->setGeometry(gripRect);
}

void PanelDockController::updateCornerGripGeometry(DockablePanel* panel) {
    DockResizeGrip* grip = m_states.value(panel).cornerGrip;
    grip->setGeometry(panel->width() - kDockResizeCornerGripSize,
                      panel->height() - kDockResizeCornerGripSize, kDockResizeCornerGripSize,
                      kDockResizeCornerGripSize);
}

}  // namespace traceview
