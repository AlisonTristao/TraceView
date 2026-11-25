#include "appnavbar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>

#include "diagnostics/notificationlog.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kNavBarHeight = 56;
constexpr int kNavIconSize = 22;
constexpr int kBadgeHeight = 14;
constexpr int kMaxShownCount = 99;

// One nav item: icon above a small label, filling an equal share of the bar.
// The Notifications one also paints the unread count over its icon, the same
// badge NotificationBell draws.
class NavItem : public QToolButton {
public:
    NavItem(const QString& text, QWidget* parent) : QToolButton(parent) {
        setObjectName("appNavItem");
        setText(text);
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        setIconSize(QSize(kNavIconSize, kNavIconSize));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
    }

    NotificationLog* log = nullptr;

protected:
    void paintEvent(QPaintEvent* event) override {
        QToolButton::paintEvent(event);
        const int unread = log != nullptr ? log->unreadCount() : 0;
        if (unread <= 0) {
            return;
        }
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        QColor fill = palette.accent;
        switch (log->unreadWorstSeverity()) {
            case StatusSeverity::Error:
                fill = palette.danger;
                break;
            case StatusSeverity::Warning:
                fill = palette.warning;
                break;
            case StatusSeverity::Info:
            case StatusSeverity::Success:
                break;
        }
        const QString text =
            unread > kMaxShownCount ? QStringLiteral("99+") : QString::number(unread);
        QFont font = scaledFont(this->font(), 0.75);
        font.setBold(true);
        const QFontMetrics metrics(font);
        const int width = qMax(kBadgeHeight, metrics.horizontalAdvance(text) + 8);
        // Top-right of the glyph, which is centred horizontally near the top.
        const QRectF badge(this->width() / 2.0 + kNavIconSize / 2.0 - width / 2.0, 4, width,
                           kBadgeHeight);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(badge, kBadgeHeight / 2.0, kBadgeHeight / 2.0);
        painter.setPen(palette.background);
        painter.setFont(font);
        painter.drawText(badge, Qt::AlignCenter, text);
    }
};

}  // namespace

AppNavBar::AppNavBar(QWidget* parent) : QWidget(parent) {
    setObjectName("appNavBar");
    // Paints the QSS background (see stylesheet.cpp) with this set.
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(kNavBarHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(false);

    const auto addItem = [this, layout](const QString& text, bool checkable) {
        auto* item = new NavItem(text, this);
        item->setCheckable(checkable);
        item->setToolTip(text);
        layout->addWidget(item, 1);
        return item;
    };
    m_home = addItem(tr("Home"), true);
    m_dashboard = addItem(tr("Dashboard"), true);
    m_robots = addItem(tr("Robots"), true);
    m_notifications = addItem(tr("Notifications"), false);
    // Checked by setCurrent(), never by the click itself: a tap only asks
    // MainWindow to switch, and the ribbon's tab change checks the item.
    for (QToolButton* item : {m_home, m_dashboard, m_robots}) {
        m_group->addButton(item);
    }
    m_robots->setVisible(false);

    connect(m_home, &QToolButton::clicked, this, [this]() {
        setCurrent(Tab::Home);
        emit homeRequested();
    });
    connect(m_dashboard, &QToolButton::clicked, this, [this]() {
        setCurrent(Tab::Dashboard);
        emit dashboardRequested();
    });
    connect(m_robots, &QToolButton::clicked, this, [this]() {
        setCurrent(Tab::Robots);
        emit robotsRequested();
    });
    connect(m_notifications, &QToolButton::clicked, this, &AppNavBar::notificationsRequested);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateIcons(); });
    updateIcons();
}

void AppNavBar::setDeveloperMode(bool developer) {
    m_robots->setVisible(developer);
    if (!developer && m_robots->isChecked()) {
        setCurrent(Tab::None);
    }
}

void AppNavBar::setCurrent(Tab tab) {
    m_home->setChecked(tab == Tab::Home);
    m_dashboard->setChecked(tab == Tab::Dashboard);
    m_robots->setChecked(tab == Tab::Robots);
}

void AppNavBar::setNotificationLog(NotificationLog* log) {
    m_log = log;
    static_cast<NavItem*>(m_notifications)->log = log;
    if (log != nullptr) {
        connect(log, &NotificationLog::unreadChanged, m_notifications,
                qOverload<>(&QWidget::update));
    }
    m_notifications->update();
}

void AppNavBar::updateIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const auto setIcon = [&palette](QToolButton* item, const QString& id) {
        item->setIcon(IconLibrary::instance().icon(id, palette.textSecondary, palette.accent,
                                                   kNavIconSize));
    };
    setIcon(m_home, QStringLiteral("lucide:house"));
    setIcon(m_dashboard, QStringLiteral("lucide:layout-dashboard"));
    setIcon(m_robots, QStringLiteral("lucide:bot"));
    setIcon(m_notifications, QStringLiteral("lucide:bell"));
}

}  // namespace traceview
