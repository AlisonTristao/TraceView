#include "workspacestrip.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme/iconlibrary.h"

namespace traceview {

namespace {

constexpr int kStripHeight = 40;
constexpr int kChipHeight = 30;
constexpr int kChipIconSize = 16;
constexpr int kChipMaxTextWidth = 160;
constexpr int kStripMargin = 8;
constexpr int kChipSpacing = 6;
constexpr int kLongPressMs = 500;
// A press that drifts further than this is a scroll drag, not a long press.
constexpr int kLongPressSlopPx = 10;

constexpr char kPlusIconId[] = "lucide:plus";

// A QToolButton that also reports a press held for kLongPressMs (a phone
// has no right-click) or a desktop context-menu request. A long press
// swallows the release, so it never also counts as a tap.
class StripChip : public QToolButton {
public:
    explicit StripChip(QWidget* parent) : QToolButton(parent) {
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
            m_pressPos = event->pos();
            m_timer.start();
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_timer.isActive() &&
            (event->pos() - m_pressPos).manhattanLength() > kLongPressSlopPx) {
            m_timer.stop();
        }
        QToolButton::mouseMoveEvent(event);
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
    QPoint m_pressPos;
    bool m_longPressed = false;
};

}  // namespace

WorkspaceStrip::WorkspaceStrip(QWidget* parent) : QWidget(parent) {
    setObjectName("workspaceStrip");
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(kStripHeight);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("workspaceStripScroll");
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(m_scroll);

    m_body = new QWidget;
    m_body->setObjectName("workspaceStripBody");
    m_layout = new QHBoxLayout(m_body);
    m_layout->setContentsMargins(kStripMargin, 0, kStripMargin, 0);
    m_layout->setSpacing(kChipSpacing);
    m_scroll->setWidget(m_body);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
}

void WorkspaceStrip::setWorkspaces(const QVector<Entry>& entries, const QString& activeId) {
    m_entries = entries;
    m_activeId = activeId;
    rebuild();
}

void WorkspaceStrip::setManagementEnabled(bool enabled) {
    if (m_managementEnabled == enabled) {
        return;
    }
    m_managementEnabled = enabled;
    rebuild();
}

void WorkspaceStrip::updateIcons(const QColor& color, const QColor& checkedColor) {
    m_iconColor = color;
    m_checkedIconColor = checkedColor;
    rebuild();
}

void WorkspaceStrip::emitDeferred(std::function<void()> emitter) {
    QMetaObject::invokeMethod(this, std::move(emitter), Qt::QueuedConnection);
}

void WorkspaceStrip::rebuild() {
    // deleteLater(), not delete: rebuild() is reached from a chip's own
    // signal handler often enough that the sender must outlive this call.
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            m_group->removeButton(qobject_cast<QAbstractButton*>(widget));
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    const auto makeChip = [this](const QString& iconId, const QString& text,
                                 const QString& toolTip) {
        auto* chip = new StripChip(m_body);
        chip->setObjectName("workspaceChip");
        chip->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly
                                                : Qt::ToolButtonTextBesideIcon);
        chip->setFixedHeight(kChipHeight);
        chip->setIconSize(QSize(kChipIconSize, kChipIconSize));
        chip->setIcon(IconLibrary::instance().icon(iconId, m_iconColor, m_checkedIconColor,
                                                   kChipIconSize));
        chip->setText(
            QFontMetrics(chip->font()).elidedText(text, Qt::ElideRight, kChipMaxTextWidth));
        chip->setToolTip(toolTip);
        return chip;
    };

    int editableCount = 0;
    for (const Entry& entry : m_entries) {
        if (!entry.builtIn) {
            ++editableCount;
        }
    }
    const bool deletable = m_managementEnabled && editableCount > 1;

    QAbstractButton* activeChip = nullptr;
    for (const Entry& entry : m_entries) {
        StripChip* chip = makeChip(entry.icon, entry.name, entry.name);
        chip->setCheckable(true);
        chip->setChecked(entry.id == m_activeId);
        m_group->addButton(chip);
        const QString id = entry.id;
        connect(chip, &QToolButton::clicked, this,
                [this, id]() { emitDeferred([this, id]() { emit workspaceSelected(id); }); });
        chip->onLongPress = [this, entry, chip, deletable]() {
            showEntryMenu(entry, chip, deletable && !entry.builtIn);
        };
        m_layout->addWidget(chip);
        if (chip->isChecked()) {
            activeChip = chip;
        }
    }

    if (m_managementEnabled) {
        StripChip* addChip =
            makeChip(QString::fromLatin1(kPlusIconId), QString(), tr("New Workspace…"));
        connect(addChip, &QToolButton::clicked, this,
                [this]() { emitDeferred([this]() { emit newWorkspaceRequested(); }); });
        m_layout->addWidget(addChip);
    }
    m_layout->addStretch(1);

    if (activeChip != nullptr) {
        // After the layout has given the new chips their geometry.
        QPointer<QAbstractButton> guard(activeChip);
        QTimer::singleShot(0, this, [this, guard]() {
            if (guard) {
                m_scroll->ensureWidgetVisible(guard, kStripMargin, 0);
            }
        });
    }
}

void WorkspaceStrip::showEntryMenu(const Entry& entry, QWidget* anchor, bool deletable) {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // The name as a (disabled) header: a chip's text may be elided.
    menu->addAction(entry.name)->setEnabled(false);

    if (m_managementEnabled && !entry.builtIn) {
        menu->addSeparator();
        const QString id = entry.id;
        connect(menu->addAction(tr("Rename…")), &QAction::triggered, this,
                [this, id]() { emitDeferred([this, id]() { emit renameRequested(id); }); });
        connect(menu->addAction(tr("Change Icon…")), &QAction::triggered, this,
                [this, id]() { emitDeferred([this, id]() { emit iconChangeRequested(id); }); });
        if (deletable) {
            connect(menu->addAction(tr("Delete Workspace")), &QAction::triggered, this, [this, id]() {
                emitDeferred([this, id]() { emit workspaceDeleteRequested(id); });
            });
        }
    }

    menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

}  // namespace traceview
