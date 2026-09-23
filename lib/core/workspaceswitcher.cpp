#include "workspaceswitcher.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QWidgetAction>
#include <functional>

#include "ribbon.h"
#include "ribbonicons.h"
#include "theme/iconlibrary.h"

namespace traceview {

namespace {

// A bit larger than kRibbonIconSize (16) -- at that size the trash glyph
// read as too small/fiddly against the row's text next to it.
constexpr int kTrashIconSize = 20;

// Extra gap between the switcher button's icon and its label. QToolButton
// has no QSS/API knob for that spacing (the style hardcodes a few px), so
// the icon is drawn onto a wider transparent canvas instead.
constexpr int kIconTextGap = 6;

QIcon padIconRight(const QIcon& icon, int size, int pad) {
    QIcon padded;
    for (const qreal dpr : {1.0, 1.5, 2.0, 3.0}) {
        const QPixmap src = icon.pixmap(QSize(size, size), dpr);
        QPixmap dst(qRound((size + pad) * dpr), qRound(size * dpr));
        dst.setDevicePixelRatio(dpr);
        dst.fill(Qt::transparent);
        QPainter painter(&dst);
        painter.drawPixmap(0, 0, src);
        painter.end();
        padded.addPixmap(dst);
    }
    return padded;
}

// One menu row: the workspace's name (click anywhere on it to select) plus
// an optional trash button. Not a QObject -- clicks are reported through a
// plain std::function rather than a signal, so this stays a header-free,
// moc-free helper local to this .cpp (the trash QToolButton is a real
// QObject already, so its own clicked() still connects normally).
class WorkspaceRow : public QWidget {
public:
    WorkspaceRow(const QString& name, bool active, bool deletable, bool iconEditable,
                 QWidget* parent)
        : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(kRibbonGroupPadding, 4, kRibbonGroupPadding, 4);
        layout->setSpacing(kRibbonGroupPadding);

        // The workspace's own icon; clicking it re-picks the icon when
        // editable. Otherwise it's purely decorative and lets the click
        // through to the row (select), like the name label does.
        m_iconButton = new QToolButton(this);
        m_iconButton->setAutoRaise(true);
        m_iconButton->setFixedSize(24, 24);
        m_iconButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
        if (iconEditable) {
            m_iconButton->setToolTip(QObject::tr("Change icon"));
        } else {
            m_iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        layout->addWidget(m_iconButton);

        auto* label = new QLabel(name, this);
        QFont font = label->font();
        font.setBold(active);
        label->setFont(font);
        layout->addWidget(label, /*stretch=*/1);

        m_trashButton = new QToolButton(this);
        m_trashButton->setAutoRaise(true);
        m_trashButton->setFixedSize(24, 24);
        m_trashButton->setIconSize(QSize(kTrashIconSize, kTrashIconSize));
        m_trashButton->setVisible(deletable);
        m_trashButton->setToolTip(QObject::tr("Delete workspace"));
        layout->addWidget(m_trashButton);
    }

    QToolButton* iconButton() const {
        return m_iconButton;
    }

    QToolButton* trashButton() const {
        return m_trashButton;
    }

    // Called when the row (but not the trash button) is clicked.
    std::function<void()> onSelected;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onSelected) {
            onSelected();
        }
        QWidget::mousePressEvent(event);
    }

private:
    QToolButton* m_iconButton = nullptr;
    QToolButton* m_trashButton = nullptr;
};

}  // namespace

WorkspaceSwitcher::WorkspaceSwitcher(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_menu = new QMenu(this);

    m_button = new QToolButton(this);
    m_button->setObjectName("workspaceSwitcherButton");
    m_button->setAutoRaise(true);
    m_button->setPopupMode(QToolButton::InstantPopup);
    m_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_button->setIconSize(QSize(kRibbonIconSize + kIconTextGap, kRibbonIconSize));
    m_button->setMenu(m_menu);
    // The style-drawn popup arrow (QToolButton::menu-indicator) otherwise
    // sits hard against/over the label with no reserved space of its own --
    // carve out room on the right and center the arrow within it instead of
    // relying on the global QToolButton QSS rule's plain 2px padding. No
    // vertical padding and no fixed height: QStatusBar sizes and centers a
    // permanent widget from its own sizeHint(), so the button is left to
    // size itself to whatever the icon/text/padding actually need instead
    // of a hardcoded number that risks starving the label's line height.
    m_button->setStyleSheet(
        "QToolButton#workspaceSwitcherButton { padding: 0px 16px 0px 4px; }"
        "QToolButton#workspaceSwitcherButton::menu-indicator {"
        "  subcontrol-origin: padding; subcontrol-position: right center; right: 4px; }");
    layout->addWidget(m_button);
}

void WorkspaceSwitcher::setWorkspaces(const QVector<Entry>& entries, const QString& activeId) {
    m_entries = entries;
    m_activeId = activeId;

    QString activeName;
    for (const Entry& entry : m_entries) {
        if (entry.id == activeId) {
            activeName = entry.name;
            break;
        }
    }
    m_button->setText(activeName);
    m_button->setToolTip(activeName);

    updateButtonIcon();
    rebuildMenu();
}

void WorkspaceSwitcher::setManagementEnabled(bool enabled) {
    if (m_managementEnabled == enabled) {
        return;
    }
    m_managementEnabled = enabled;
    rebuildMenu();
}

void WorkspaceSwitcher::rebuildMenu() {
    m_menu->clear();

    int editableCount = 0;
    for (const Entry& entry : m_entries) {
        if (!entry.builtIn) {
            ++editableCount;
        }
    }
    const bool deletable = m_managementEnabled && editableCount > 1;
    for (const Entry& entry : m_entries) {
        const QString id = entry.id;
        const bool iconEditable = m_managementEnabled && !entry.builtIn;
        auto* row = new WorkspaceRow(entry.name, id == m_activeId,
                                     deletable && !entry.builtIn, iconEditable, m_menu);
        row->iconButton()->setIcon(
            IconLibrary::instance().icon(entry.icon, m_iconColor, kRibbonIconSize));
        row->trashButton()->setIcon(makeTrashIcon(m_iconColor, kTrashIconSize));
        // Deferred to the next event-loop turn (rather than emitted straight
        // from here): both handlers below end up back in rebuildMenu() via
        // MainWindow -> refreshWorkspaceSwitcher(), whose m_menu->clear()
        // deletes every WorkspaceRow -- including whichever one is still
        // sitting on the call stack inside its own mousePressEvent (row
        // click) or the trash QToolButton's click handling (delete click).
        // Running that synchronously used to delete `this` out from under
        // the very call that triggered it, e.g. WorkspaceRow::
        // mousePressEvent() touching `this` again (via QWidget::
        // mousePressEvent(event)) right after onSelected() returned --
        // an intermittent use-after-free crash reproduced by clicking
        // through the switcher a few times.
        row->onSelected = [this, id]() {
            QMetaObject::invokeMethod(
                this,
                [this, id]() {
                    m_menu->close();
                    emit workspaceSelected(id);
                },
                Qt::QueuedConnection);
        };
        if (iconEditable) {
            connect(row->iconButton(), &QToolButton::clicked, this, [this, id]() {
                QMetaObject::invokeMethod(
                    this,
                    [this, id]() {
                        m_menu->close();
                        emit iconChangeRequested(id);
                    },
                    Qt::QueuedConnection);
            });
        }
        connect(row->trashButton(), &QToolButton::clicked, this, [this, id]() {
            QMetaObject::invokeMethod(
                this,
                [this, id]() {
                    m_menu->close();
                    emit workspaceDeleteRequested(id);
                },
                Qt::QueuedConnection);
        });

        auto* action = new QWidgetAction(m_menu);
        action->setDefaultWidget(row);
        m_menu->addAction(action);
    }

    if (m_managementEnabled) {
        m_menu->addSeparator();
        QAction* newWorkspaceAction = m_menu->addAction(tr("New Workspace…"));
        connect(newWorkspaceAction, &QAction::triggered, this,
                &WorkspaceSwitcher::newWorkspaceRequested);
    }
}

void WorkspaceSwitcher::updateIcons(const QColor& color) {
    m_iconColor = color;
    updateButtonIcon();
    rebuildMenu();
}

void WorkspaceSwitcher::updateButtonIcon() {
    // The active workspace's picked icon; the generic workspace glyph only
    // while nothing resolves (entries not fed yet, or an unknown id).
    QIcon icon;
    for (const Entry& entry : m_entries) {
        if (entry.id == m_activeId) {
            icon = IconLibrary::instance().icon(entry.icon, m_iconColor, kRibbonIconSize);
            break;
        }
    }
    if (icon.isNull()) {
        icon = makeWorkspaceIcon(m_iconColor);
    }
    m_button->setIcon(padIconRight(icon, kRibbonIconSize, kIconTextGap));
}

}  // namespace traceview
