#pragma once

#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMargins>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <initializer_list>

class QAbstractButton;
class QBoxLayout;
class QDialogButtonBox;
class QHBoxLayout;
class QVBoxLayout;

namespace traceview {

// The parts every dialog is built from, so all of them look like one app:
// the TraceKit panel's look (header with title, subtitle and status badge,
// a side list of pages, cards, one footer), made reusable. The colors and
// shapes live in the app stylesheet (theme/stylesheet.cpp, "Dialog chrome"),
// keyed by the object names these widgets set -- so they follow the palette
// and the frame style (Square/Chamfered make them square) with no local QSS.
// How to build a new dialog with them: CONTRIBUTING.md, "Dialogs".
namespace DialogChrome {

// Outer margins and spacing of a dialog's root layout.
inline constexpr QMargins kMargins{16, 14, 16, 12};
inline constexpr int kSpacing = 10;
// Between the side list and its pages, and between stacked cards/sections.
inline constexpr int kBodySpacing = 14;
inline constexpr int kSectionSpacing = 12;
// Side list.
inline constexpr int kNavWidth = 176;
inline constexpr int kNavCompactWidth = 52;
inline constexpr int kNavRowHeight = 36;
inline constexpr int kNavIconSize = 18;
// Font scales (FontManager's scaledFont(), never a pt size in QSS).
inline constexpr qreal kTitleScale = 1.45;
inline constexpr qreal kPageTitleScale = 1.15;
inline constexpr qreal kCardTitleScale = 0.85;

// Applies kMargins/kSpacing to `dialog`'s new root QVBoxLayout and returns it.
QVBoxLayout* setupRootLayout(QWidget* dialog);

// Secondary text (status lines, explanations): word-wrapped, muted color.
QLabel* hintLabel(const QString& text, QWidget* parent);

// "primary" (filled accent: the one main action of a page), "danger"
// (destructive: delete, erase, reset), "success", "warning"; empty clears it.
void setButtonVariant(QAbstractButton* button, const char* variant);

// The bottom row: `leading` buttons on the left (page actions such as
// Add/Remove/Copy), a stretch, then the dialog's standard buttons.
QHBoxLayout* footer(QDialogButtonBox* buttons, std::initializer_list<QWidget*> leading = {});

}  // namespace DialogChrome

// A small pill with a dot: connection state, install state...
class StatusBadge : public QLabel {
    Q_OBJECT

public:
    enum class Tone { Neutral, Accent, Success, Warning, Danger };

    explicit StatusBadge(QWidget* parent = nullptr);
    void setStatus(const QString& text, Tone tone);
    Tone tone() const { return m_tone; }

private:
    Tone m_tone = Tone::Neutral;
};

// Title + optional subtitle on the left, badge or other widgets on the
// right.
//   Level::Window -- the top of a dialog. While the dialog is embedded
//                    (DialogPresenter on a phone) the presenter's own top bar
//                    already shows the window title, so the title line hides
//                    itself there and only the subtitle/badge remain.
//   Level::Page   -- the top of one page of a NavSidebar dialog: smaller
//                    title, always shown, the page's description below.
class DialogHeader : public QWidget {
    Q_OBJECT

public:
    enum class Level { Window, Page };

    explicit DialogHeader(const QString& title, const QString& subtitle = QString(),
                          QWidget* parent = nullptr, Level level = Level::Window);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    // Created on first use, at the right end of the header.
    StatusBadge* badge();
    void addTrailingWidget(QWidget* widget);

protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void refreshFonts();

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    StatusBadge* m_badge = nullptr;
    QHBoxLayout* m_layout = nullptr;
    Level m_level = Level::Window;
};

// The side list of pages. Icons are Lucide ids ("lucide:wifi"), tinted
// from the palette and re-tinted when the theme changes. Compact mode
// (phone width) shows only the icons; the name stays in the tooltip.
class NavSidebar : public QListWidget {
    Q_OBJECT

public:
    explicit NavSidebar(QWidget* parent = nullptr);

    // Appends (row < 0) or inserts a page entry; returns its row.
    int addPage(const QString& title, const QString& iconId, int row = -1);
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }

private:
    void refreshIcons();

    QStringList m_titles;
    QStringList m_icons;
    bool m_compact = false;
};

// A titled card: a surface panel with a small caption on top, for grouping
// related content on a page (the Firmware page's Installed/File/Install).
class DialogCard : public QFrame {
    Q_OBJECT

public:
    explicit DialogCard(const QString& title = QString(), QWidget* parent = nullptr);

    QVBoxLayout* body() const { return m_body; }
    // For a card that outlives a language change.
    void setTitle(const QString& title);
    // Right end of the caption row (a badge, a small button).
    void addTitleWidget(QWidget* widget);

protected:
    void changeEvent(QEvent* event) override;

private:
    QLabel* m_title = nullptr;
    QHBoxLayout* m_titleRow = nullptr;
    QVBoxLayout* m_body = nullptr;
};

}  // namespace traceview
