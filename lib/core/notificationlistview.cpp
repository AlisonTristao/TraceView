#include "core/notificationlistview.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "core/ribbon.h"
#include "core/ribbonicons.h"
#include "diagnostics/notificationlog.h"
#include "preferences/appsettings.h"
#include "theme/dialogchrome.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/framestyle.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

enum Role {
    kIdRole = Qt::UserRole,
    kSeverityRole,
    kSourceRole,
    kTimeRole,
};

enum Filter { kFilterAll, kFilterWarnings, kFilterErrors };

constexpr int kPadding = 10;
constexpr int kStripe = 4;
constexpr int kCloseSize = 14;
constexpr qreal kCardRadius = 6.0;

QColor severityColor(StatusSeverity severity, const ThemePalette& palette) {
    switch (severity) {
        case StatusSeverity::Success:
            return palette.success;
        case StatusSeverity::Warning:
            return palette.warning;
        case StatusSeverity::Error:
            return palette.danger;
        case StatusSeverity::Info:
            break;
    }
    return palette.accent;
}

int wrappedHeight(const QString& text, const QFont& font, int width) {
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(qMax(1, width));
        height += line.height();
    }
    layout.endLayout();
    return int(std::ceil(height));
}

// Paints each notification as a card, the same language as the dashboard's
// pop-up cards (NotificationToasts): a severity stripe on the left, source
// and time above the text, and an X at the top right that dismisses it.
class NotificationCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

signals:
    void dismissRequested(quint64 id);

public:
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QFont meta = metaFont(option.font);
        const int textWidth = option.rect.width() - kStripe - 3 * kPadding - kCloseSize;
        const int height = kPadding + QFontMetrics(meta).height() + 4 +
                           wrappedHeight(index.data(Qt::DisplayRole).toString(), option.font, textWidth) +
                           kPadding;
        return QSize(option.rect.width(), height);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        const auto severity = StatusSeverity(index.data(kSeverityRole).toInt());
        const QRectF card = QRectF(option.rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = kCardRadius * ThemeManager::instance().currentFrameStyle().controlRadiusScale;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(card, radius, radius);
        painter->fillPath(path, palette.surface);
        painter->setClipPath(path);
        painter->fillRect(QRectF(card.left(), card.top(), kStripe, card.height()),
                          severityColor(severity, palette));
        painter->setClipping(false);
        painter->setPen(palette.border);
        painter->drawPath(path);

        const QFont meta = metaFont(option.font);
        const int left = option.rect.left() + kStripe + kPadding;
        const int right = option.rect.right() - kPadding - kCloseSize - kPadding;
        int y = option.rect.top() + kPadding;

        QString metaText = index.data(kTimeRole).toDateTime().toString(QStringLiteral("HH:mm:ss"));
        const QString source = index.data(kSourceRole).toString();
        if (!source.isEmpty()) metaText = source + QStringLiteral("  ·  ") + metaText;
        painter->setFont(meta);
        painter->setPen(palette.textSecondary);
        const int metaHeight = QFontMetrics(meta).height();
        painter->drawText(QRect(left, y, right - left, metaHeight), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(meta).elidedText(metaText, Qt::ElideRight, right - left));
        y += metaHeight + 4;

        painter->setFont(option.font);
        painter->setPen(palette.textPrimary);
        painter->drawText(QRect(left, y, right - left, option.rect.bottom() - y),
                          Qt::TextWrapAnywhere | Qt::TextWordWrap, index.data(Qt::DisplayRole).toString());

        IconLibrary::instance()
            .icon(QStringLiteral("lucide:x"), palette.textSecondary, kCloseSize)
            .paint(painter, closeRect(option.rect));
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton &&
                closeRect(option.rect).adjusted(-4, -4, 4, 4).contains(mouse->position().toPoint())) {
                emit dismissRequested(index.data(kIdRole).toULongLong());
                return true;
            }
        }
        return false;
    }

private:
    static QFont metaFont(const QFont& base) {
        return scaledFont(base, 0.85);
    }
    static QRect closeRect(const QRect& item) {
        return QRect(item.right() - kPadding - kCloseSize + 1, item.top() + kPadding, kCloseSize, kCloseSize);
    }
};

QToolButton* toolButton(QWidget* parent, const QString& tip) {
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    button->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    button->setToolTip(tip);
    return button;
}

}  // namespace

NotificationListView::NotificationListView(NotificationLog* log, bool showPin, QWidget* parent)
    : QWidget(parent), m_log(log) {
    setObjectName(QStringLiteral("notificationList"));

    auto* title = new QLabel(tr("Notifications"), this);
    QFont titleFont = scaledFont(title->font(), DialogChrome::kPageTitleScale);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);

    m_pinButton = toolButton(this, tr("Keep the panel open"));
    m_pinButton->setCheckable(true);
    m_pinButton->setVisible(showPin);
    connect(m_pinButton, &QToolButton::toggled, this, [this](bool pinned) {
        refreshIcons();
        emit pinToggled(pinned);
    });

    m_clearButton = toolButton(this, tr("Clear every notification"));
    connect(m_clearButton, &QToolButton::clicked, m_log, &NotificationLog::clear);

    m_optionsButton = toolButton(this, tr("Notification options"));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    // The chevron/indicator would only crowd a 26px icon button.
    m_optionsButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
    auto* menu = new QMenu(m_optionsButton);
    menu->addSection(tr("Pop-ups over the dashboard"));
    auto* popupGroup = new QActionGroup(menu);
    QAction* popupAll = menu->addAction(tr("Every notification"));
    QAction* popupBell = menu->addAction(tr("Only the bell"));
    QAction* errorPopups = menu->addAction(tr("Errors still pop up"));
    menu->addSection(tr("Pinned panel"));
    auto* pinGroup = new QActionGroup(menu);
    QAction* pinOverlay = menu->addAction(tr("Over the dashboard"));
    QAction* pinPush = menu->addAction(tr("Beside the dashboard (shrinks it)"));
    // Auto-delete: presets here; Settings > Diagnostics takes any number of
    // seconds, and a value set there shows as its own checked entry.
    menu->addSection(tr("Delete automatically"));
    auto* ageGroup = new QActionGroup(menu);
    const auto ageLabel = [this](int seconds) {
        if (seconds <= 0) return tr("Never");
        if (seconds % 60 == 0) return tr("After %n min", nullptr, seconds / 60);
        return tr("After %n s", nullptr, seconds);
    };
    QList<QAction*> ageActions;
    for (const int seconds : {0, 10, 30, 60, 300}) {
        QAction* action = menu->addAction(ageLabel(seconds));
        action->setData(seconds);
        ageActions.append(action);
    }
    QAction* ageCustom = menu->addAction(QString());
    ageActions.append(ageCustom);
    for (QAction* action : ageActions) {
        action->setCheckable(true);
        ageGroup->addAction(action);
        connect(action, &QAction::triggered, this, [action] {
            AppSettings::instance().setNotificationAutoDeleteSeconds(action->data().toInt());
        });
    }
    menu->addSeparator();
    QAction* history = menu->addAction(tr("Full history\xE2\x80\xA6"));
    for (QAction* action : {popupAll, popupBell, errorPopups, pinOverlay, pinPush}) action->setCheckable(true);
    popupGroup->addAction(popupAll);
    popupGroup->addAction(popupBell);
    pinGroup->addAction(pinOverlay);
    pinGroup->addAction(pinPush);
    pinOverlay->setVisible(showPin);
    pinPush->setVisible(showPin);
    connect(menu, &QMenu::aboutToShow, this, [=]() {
        const AppSettings& settings = AppSettings::instance();
        const bool bellOnly = settings.notificationPopups() == AppSettings::NotificationPopups::BellOnly;
        popupBell->setChecked(bellOnly);
        popupAll->setChecked(!bellOnly);
        errorPopups->setChecked(settings.notificationErrorPopups());
        errorPopups->setEnabled(bellOnly);
        const bool push = settings.notificationPinStyle() == AppSettings::NotificationPinStyle::Push;
        pinPush->setChecked(push);
        pinOverlay->setChecked(!push);
        const int age = settings.notificationAutoDeleteSeconds();
        bool preset = false;
        for (QAction* action : ageActions) {
            if (action != ageCustom && action->data().toInt() == age) {
                action->setChecked(true);
                preset = true;
            }
        }
        ageCustom->setVisible(!preset);
        if (!preset) {
            ageCustom->setText(ageLabel(age));
            ageCustom->setData(age);
            ageCustom->setChecked(true);
        }
    });
    connect(popupAll, &QAction::triggered, this, [] {
        AppSettings::instance().setNotificationPopups(AppSettings::NotificationPopups::All);
    });
    connect(popupBell, &QAction::triggered, this, [] {
        AppSettings::instance().setNotificationPopups(AppSettings::NotificationPopups::BellOnly);
    });
    connect(errorPopups, &QAction::toggled, this,
            [](bool on) { AppSettings::instance().setNotificationErrorPopups(on); });
    connect(pinOverlay, &QAction::triggered, this, [] {
        AppSettings::instance().setNotificationPinStyle(AppSettings::NotificationPinStyle::Overlay);
    });
    connect(pinPush, &QAction::triggered, this, [] {
        AppSettings::instance().setNotificationPinStyle(AppSettings::NotificationPinStyle::Push);
    });
    connect(history, &QAction::triggered, this, &NotificationListView::historyRequested);
    m_optionsButton->setMenu(menu);

    m_closeButton = toolButton(this, tr("Close"));
    connect(m_closeButton, &QToolButton::clicked, this, &NotificationListView::closeRequested);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(2);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(m_pinButton);
    titleRow->addWidget(m_clearButton);
    titleRow->addWidget(m_optionsButton);
    titleRow->addWidget(m_closeButton);

    m_filter = new QComboBox(this);
    m_filter->addItem(tr("All notifications"), kFilterAll);
    m_filter->addItem(tr("Warnings and errors"), kFilterWarnings);
    m_filter->addItem(tr("Errors"), kFilterErrors);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &NotificationListView::applyFilter);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("notificationCards"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setSpacing(3);
    auto* delegate = new NotificationCardDelegate(m_list);
    m_list->setItemDelegate(delegate);
    connect(delegate, &NotificationCardDelegate::dismissRequested, m_log, &NotificationLog::remove);

    m_empty = DialogChrome::hintLabel(tr("No notifications."), this);
    m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(8);
    layout->addLayout(titleRow);
    layout->addWidget(m_filter);
    layout->addWidget(m_empty);
    layout->addWidget(m_list, 1);

    connect(m_log, &NotificationLog::entryAdded, this,
            [this](const NotificationEntry& entry, bool droppedOldest) {
                if (droppedOldest && m_list->count() > 0) delete m_list->takeItem(m_list->count() - 1);
                addRow(entry);
                applyFilter();
            });
    connect(m_log, &NotificationLog::cleared, this, &NotificationListView::rebuild);
    connect(m_log, &NotificationLog::removed, this, [this](quint64 id) {
        for (int row = 0; row < m_list->count(); ++row) {
            if (m_list->item(row)->data(kIdRole).toULongLong() == id) {
                delete m_list->takeItem(row);
                break;
            }
        }
        refreshEmptyState();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](const ThemePalette&) {
        refreshIcons();
        m_list->viewport()->update();
    });
    refreshIcons();
    rebuild();
}

void NotificationListView::setPinned(bool pinned) {
    const QSignalBlocker blocker(m_pinButton);
    m_pinButton->setChecked(pinned);
    refreshIcons();
}

void NotificationListView::rebuild() {
    m_list->clear();
    for (const NotificationEntry& entry : m_log->entries()) addRow(entry);
    applyFilter();
}

void NotificationListView::addRow(const NotificationEntry& entry) {
    // Newest first.
    auto* item = new QListWidgetItem(entry.text);
    item->setData(kIdRole, QVariant::fromValue<qulonglong>(entry.id));
    item->setData(kSeverityRole, int(entry.severity));
    item->setData(kSourceRole, entry.source);
    item->setData(kTimeRole, entry.timestamp);
    item->setToolTip(entry.text);
    m_list->insertItem(0, item);
}

void NotificationListView::applyFilter() {
    const int filter = m_filter->currentData().toInt();
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        const int rank = notificationSeverityRank(StatusSeverity(item->data(kSeverityRole).toInt()));
        const bool shown = filter == kFilterAll ||
                           (filter == kFilterWarnings && rank >= notificationSeverityRank(StatusSeverity::Warning)) ||
                           (filter == kFilterErrors && rank >= notificationSeverityRank(StatusSeverity::Error));
        item->setHidden(!shown);
    }
    refreshEmptyState();
}

void NotificationListView::refreshEmptyState() {
    bool any = false;
    for (int row = 0; row < m_list->count() && !any; ++row) any = !m_list->item(row)->isHidden();
    m_empty->setVisible(!any);
    m_list->setVisible(any);
    m_clearButton->setEnabled(m_list->count() > 0);
}

void NotificationListView::refreshIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const IconLibrary& icons = IconLibrary::instance();
    m_pinButton->setIcon(icons.icon(m_pinButton->isChecked() ? QStringLiteral("lucide:pin")
                                                              : QStringLiteral("lucide:pin-off"),
                                    palette.textPrimary, palette.accent, kRibbonIconSize));
    m_pinButton->setToolTip(m_pinButton->isChecked() ? tr("Pinned: the panel stays open")
                                                     : tr("Keep the panel open"));
    m_clearButton->setIcon(icons.icon(QStringLiteral("lucide:trash-2"), palette.danger, kRibbonIconSize));
    m_optionsButton->setIcon(icons.icon(QStringLiteral("lucide:settings-2"), palette.textPrimary, kRibbonIconSize));
    m_closeButton->setIcon(icons.icon(QStringLiteral("lucide:x"), palette.textPrimary, kRibbonIconSize));
}

}  // namespace traceview

#include "notificationlistview.moc"
