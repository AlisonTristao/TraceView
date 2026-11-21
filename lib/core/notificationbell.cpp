#include "core/notificationbell.h"

#include <QPainter>

#include "core/ribbon.h"
#include "core/ribbonicons.h"
#include "diagnostics/notificationlog.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kBadgeHeight = 13;
constexpr int kMaxShownCount = 99;
}  // namespace

NotificationBell::NotificationBell(NotificationLog* log, QWidget* parent)
    : QToolButton(parent), m_log(log) {
    setObjectName(QStringLiteral("notificationBell"));
    setCheckable(true);
    setAutoRaise(true);
    setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    connect(m_log, &NotificationLog::unreadChanged, this, &NotificationBell::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { refresh(); });
    refresh();
}

void NotificationBell::refresh() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const int unread = m_log->unreadCount();
    setIcon(IconLibrary::instance().icon(
        unread > 0 ? QStringLiteral("lucide:bell-dot") : QStringLiteral("lucide:bell"),
        palette.textPrimary, palette.accent, kRibbonIconSize));
    setToolTip(unread > 0 ? tr("Notifications: %n unread", nullptr, unread) : tr("Notifications"));
    update();
}

void NotificationBell::paintEvent(QPaintEvent* event) {
    QToolButton::paintEvent(event);
    const int unread = m_log->unreadCount();
    if (unread <= 0) return;

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QColor fill = palette.accent;
    switch (m_log->unreadWorstSeverity()) {
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

    const QString text = unread > kMaxShownCount ? QStringLiteral("99+") : QString::number(unread);
    QFont font = scaledFont(this->font(), 0.7);
    font.setBold(true);
    const QFontMetrics metrics(font);
    const int width = qMax(kBadgeHeight, metrics.horizontalAdvance(text) + 6);
    // Top-right corner, overhanging the glyph a little like a phone badge.
    const QRectF badge(this->width() - width, 0, width, kBadgeHeight);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(badge, kBadgeHeight / 2.0, kBadgeHeight / 2.0);
    painter.setPen(palette.background);
    painter.setFont(font);
    painter.drawText(badge, Qt::AlignCenter, text);
}

}  // namespace traceview
