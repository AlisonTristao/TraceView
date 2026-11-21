#include "theme/dialogchrome.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

#include "theme/dialogpresenter.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// Re-reads the stylesheet after a dynamic property the QSS selects on
// changed -- Qt only applies property selectors at polish time.
void repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

const char* toneName(StatusBadge::Tone tone) {
    switch (tone) {
        case StatusBadge::Tone::Accent:
            return "accent";
        case StatusBadge::Tone::Success:
            return "success";
        case StatusBadge::Tone::Warning:
            return "warning";
        case StatusBadge::Tone::Danger:
            return "danger";
        case StatusBadge::Tone::Neutral:
            break;
    }
    return "neutral";
}

QFont semiBold(QFont font) {
    font.setWeight(QFont::DemiBold);
    return font;
}

}  // namespace

// --- DialogChrome ----------------------------------------------------------

QVBoxLayout* DialogChrome::setupRootLayout(QWidget* dialog) {
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(kMargins);
    layout->setSpacing(kSpacing);
    return layout;
}

QLabel* DialogChrome::hintLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("dialogHint"));
    label->setWordWrap(true);
    return label;
}

void DialogChrome::setButtonVariant(QAbstractButton* button, const char* variant) {
    if (button == nullptr) return;
    button->setProperty("variant", QString::fromLatin1(variant));
    repolish(button);
}

QHBoxLayout* DialogChrome::footer(QDialogButtonBox* buttons, std::initializer_list<QWidget*> leading) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    for (QWidget* widget : leading) row->addWidget(widget);
    row->addStretch(1);
    if (buttons != nullptr) row->addWidget(buttons);
    return row;
}

// --- StatusBadge -----------------------------------------------------------

StatusBadge::StatusBadge(QWidget* parent) : QLabel(parent) {
    setObjectName(QStringLiteral("statusBadge"));
    setProperty("tone", QString::fromLatin1(toneName(m_tone)));
}

void StatusBadge::setStatus(const QString& text, Tone tone) {
    setText(QStringLiteral("●  ") + text);  // a dot in the badge's own color
    if (tone == m_tone) return;
    m_tone = tone;
    setProperty("tone", QString::fromLatin1(toneName(tone)));
    repolish(this);
}

// --- DialogHeader ----------------------------------------------------------

DialogHeader::DialogHeader(const QString& title, const QString& subtitle, QWidget* parent, Level level)
    : QWidget(parent), m_level(level) {
    setObjectName(QStringLiteral("dialogHeader"));
    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("dialogTitle"));
    m_title->setWordWrap(true);
    m_subtitle = new QLabel(subtitle, this);
    m_subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    m_subtitle->setWordWrap(true);
    m_subtitle->setVisible(!subtitle.isEmpty());

    auto* titles = new QVBoxLayout();
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 4);
    m_layout->setSpacing(DialogChrome::kSpacing);
    m_layout->addLayout(titles, 1);
    refreshFonts();
}

void DialogHeader::setTitle(const QString& title) {
    m_title->setText(title);
}

void DialogHeader::setSubtitle(const QString& subtitle) {
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

StatusBadge* DialogHeader::badge() {
    if (m_badge == nullptr) {
        m_badge = new StatusBadge(this);
        m_layout->addWidget(m_badge, 0, Qt::AlignTop);
    }
    return m_badge;
}

void DialogHeader::addTrailingWidget(QWidget* widget) {
    widget->setParent(this);
    m_layout->addWidget(widget, 0, Qt::AlignTop);
}

void DialogHeader::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Decided at show time: the same dialog can open embedded (phone,
    // compact preview) one time and as a window the next.
    if (m_level == Level::Window) m_title->setVisible(!DialogPresenter::embedded());
}

void DialogHeader::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) refreshFonts();
}

void DialogHeader::refreshFonts() {
    // From the header's own (inherited) font, so a change in View > Font
    // reaches the explicitly set title font too.
    const qreal scale = m_level == Level::Window ? DialogChrome::kTitleScale : DialogChrome::kPageTitleScale;
    m_title->setFont(semiBold(scaledFont(font(), scale)));
}

// --- NavSidebar ------------------------------------------------------------

NavSidebar::NavSidebar(QWidget* parent) : QListWidget(parent) {
    setObjectName(QStringLiteral("dialogNav"));
    setFixedWidth(DialogChrome::kNavWidth);
    setIconSize(QSize(DialogChrome::kNavIconSize, DialogChrome::kNavIconSize));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { refreshIcons(); });
}

int NavSidebar::addPage(const QString& title, const QString& iconId, int row) {
    if (row < 0 || row > count()) row = count();
    auto* item = new QListWidgetItem(m_compact ? QString() : title);
    item->setToolTip(title);
    item->setSizeHint(QSize(0, DialogChrome::kNavRowHeight));
    insertItem(row, item);
    m_titles.insert(row, title);
    m_icons.insert(row, iconId);
    refreshIcons();
    return row;
}

void NavSidebar::setCompact(bool compact) {
    if (compact == m_compact) return;
    m_compact = compact;
    setFixedWidth(compact ? DialogChrome::kNavCompactWidth : DialogChrome::kNavWidth);
    // The stylesheet centres the icon in compact rows (see "Dialog chrome").
    setProperty("compact", compact);
    style()->unpolish(this);
    style()->polish(this);
    for (int row = 0; row < count() && row < m_titles.size(); ++row) {
        item(row)->setText(compact ? QString() : m_titles.at(row));
    }
}

void NavSidebar::refreshIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const int size = DialogChrome::kNavIconSize;
    for (int row = 0; row < count() && row < m_icons.size(); ++row) {
        QIcon icon;
        const QIcon normal = IconLibrary::instance().icon(m_icons.at(row), palette.textSecondary, size);
        const QIcon selected = IconLibrary::instance().icon(m_icons.at(row), palette.accent, size);
        icon.addPixmap(normal.pixmap(size, size), QIcon::Normal);
        icon.addPixmap(selected.pixmap(size, size), QIcon::Selected);
        item(row)->setIcon(icon);
    }
}

// --- DialogCard ------------------------------------------------------------

DialogCard::DialogCard(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("dialogCard"));
    // A caption, not a heading: small, muted, upper case.
    m_title = new QLabel(title.toUpper(), this);
    m_title->setObjectName(QStringLiteral("dialogCardTitle"));
    m_title->setFont(semiBold(scaledFont(font(), DialogChrome::kCardTitleScale)));

    m_titleRow = new QHBoxLayout();
    m_titleRow->setContentsMargins(0, 0, 0, 0);
    m_titleRow->addWidget(m_title, 1);

    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(16, 14, 16, 14);
    m_body->setSpacing(10);
    m_body->addLayout(m_titleRow);
    m_title->setVisible(!title.isEmpty());
}

void DialogCard::addTitleWidget(QWidget* widget) {
    widget->setParent(this);
    m_titleRow->addWidget(widget, 0, Qt::AlignVCenter);
    m_title->setVisible(true);
}

void DialogCard::changeEvent(QEvent* event) {
    QFrame::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        m_title->setFont(semiBold(scaledFont(font(), DialogChrome::kCardTitleScale)));
    }
}

}  // namespace traceview
