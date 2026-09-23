#include "workspacedock.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <functional>

#include "ribbon.h"
#include "theme/iconlibrary.h"

namespace traceview {

namespace {

// m_statusRow's own height (one ribbon button plus its vertical margins),
// doubled -- the dock takes that row's place in compact chrome -- then the
// whole dock (bar, buttons, glyphs) scaled down by kDockScale, which read
// as too chunky at full size.
constexpr double kDockScale = 0.8;
constexpr int kStatusRowHeight = kRibbonButtonSize + 2 * kRibbonPageMarginV;
constexpr int kDockHeight = int(2 * kStatusRowHeight * kDockScale);  // 51
// Scaled from Material's 48dp touch target and its usual 24dp glyph.
constexpr int kDockButtonSize = int(48 * kDockScale);  // 38
constexpr int kDockIconSize = int(24 * kDockScale);    // 19
constexpr int kLongPressMs = 500;

constexpr char kPlusIconId[] = "lucide:plus";

// A QToolButton that also reports a press held for kLongPressMs (a phone
// has no right-click) or a desktop context-menu request. A long press
// swallows the release, so it never also counts as a tap. Not a Q_OBJECT
// -- the callback is a plain std::function, same as WorkspaceSwitcher's
// WorkspaceRow.
class DockButton : public QToolButton {
public:
    explicit DockButton(QWidget* parent) : QToolButton(parent) {
        m_timer.setSingleShot(true);
        m_timer.setInterval(kLongPressMs);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_longPressed = true;
            setDown(false);
            if (onLongPress) {
                onLongPress();
            }
        });
    }

    std::function<void()> onLongPress;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        m_longPressed = false;
        if (event->button() == Qt::LeftButton && onLongPress) {
            m_timer.start();
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_timer.stop();
        if (m_longPressed) {
            m_longPressed = false;
            event->accept();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        if (onLongPress) {
            onLongPress();
            event->accept();
        }
    }

private:
    QTimer m_timer;
    bool m_longPressed = false;
};

}  // namespace

WorkspaceDock::WorkspaceDock(QWidget* parent) : QWidget(parent) {
    setObjectName("workspaceDock");
    // A plain QWidget subclass only paints its QSS background (the bar
    // color shared with m_statusRow, see stylesheet.cpp) with this set.
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(kDockHeight);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(kRibbonPageMarginH, 0, kRibbonPageMarginH, 0);
    m_layout->setSpacing(0);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
}

void WorkspaceDock::setWorkspaces(const QVector<WorkspaceSwitcher::Entry>& entries,
                                  const QString& activeId) {
    m_entries = entries;
    m_activeId = activeId;
    rebuild();
}

void WorkspaceDock::setManagementEnabled(bool enabled) {
    if (m_managementEnabled == enabled) {
        return;
    }
    m_managementEnabled = enabled;
    rebuild();
}

void WorkspaceDock::updateIcons(const QColor& color, const QColor& checkedColor) {
    m_iconColor = color;
    m_checkedIconColor = checkedColor;
    rebuild();
}

void WorkspaceDock::emitDeferred(std::function<void()> emitter) {
    QMetaObject::invokeMethod(this, std::move(emitter), Qt::QueuedConnection);
}

void WorkspaceDock::rebuild() {
    // deleteLater(), not delete: rebuild() is reached from a button's own
    // signal handler often enough (theme change mid-press, mode change from
    // its menu) that the sender must outlive this call.
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            m_group->removeButton(qobject_cast<QAbstractButton*>(widget));
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    const auto makeButton = [this](const QString& iconId, const QString& toolTip) {
        auto* button = new DockButton(this);
        button->setAutoRaise(true);
        button->setFixedSize(kDockButtonSize, kDockButtonSize);
        button->setIconSize(QSize(kDockIconSize, kDockIconSize));
        button->setIcon(
            IconLibrary::instance().icon(iconId, m_iconColor, m_checkedIconColor, kDockIconSize));
        button->setToolTip(toolTip);
        return button;
    };

    int editableCount = 0;
    for (const WorkspaceSwitcher::Entry& entry : m_entries) {
        if (!entry.builtIn) {
            ++editableCount;
        }
    }
    const bool deletable = m_managementEnabled && editableCount > 1;

    // Equal stretches before, between and after every button: space-evenly,
    // which keeps the row centered whatever the count.
    m_layout->addStretch(1);
    for (const WorkspaceSwitcher::Entry& entry : m_entries) {
        auto* button = makeButton(entry.icon, entry.name);
        button->setCheckable(true);
        button->setChecked(entry.id == m_activeId);
        m_group->addButton(button);
        const QString id = entry.id;
        connect(button, &QToolButton::clicked, this,
                [this, id]() { emitDeferred([this, id]() { emit workspaceSelected(id); }); });
        button->onLongPress = [this, entry, button, deletable]() {
            showEntryMenu(entry, button, deletable && !entry.builtIn);
        };
        m_layout->addWidget(button);
        m_layout->addStretch(1);
    }

    if (m_managementEnabled) {
        auto* addButton = makeButton(QString::fromLatin1(kPlusIconId), tr("New Workspace…"));
        connect(addButton, &QToolButton::clicked, this,
                [this]() { emitDeferred([this]() { emit newWorkspaceRequested(); }); });
        m_layout->addWidget(addButton);
        m_layout->addStretch(1);
    }
}

void WorkspaceDock::showEntryMenu(const WorkspaceSwitcher::Entry& entry, QWidget* anchor,
                                  bool deletable) {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // Icon-only buttons: the name as a (disabled) header is how a phone,
    // which has no hover tooltips, learns which workspace this is.
    menu->addAction(entry.name)->setEnabled(false);

    if (m_managementEnabled && !entry.builtIn) {
        menu->addSeparator();
        const QString id = entry.id;
        connect(menu->addAction(tr("Change Icon…")), &QAction::triggered, this,
                [this, id]() { emitDeferred([this, id]() { emit iconChangeRequested(id); }); });
        if (deletable) {
            connect(menu->addAction(tr("Delete Workspace")), &QAction::triggered, this, [this, id]() {
                emitDeferred([this, id]() { emit workspaceDeleteRequested(id); });
            });
        }
    }

    // Opens upward: the dock sits at the bottom edge of the screen.
    const QPoint topLeft = anchor->mapToGlobal(QPoint(0, 0));
    menu->popup(QPoint(topLeft.x(), topLeft.y() - menu->sizeHint().height()));
}

}  // namespace traceview
