#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QTabBar;
class QStackedWidget;

namespace traceview {

// Sizing conventions shared by every ribbon page and button group, so page
// content built by callers doesn't each pick its own numbers.
inline constexpr int kRibbonButtonSize = 26;
// Padding inside a button group frame, and gap between buttons within it.
inline constexpr int kRibbonGroupPadding = 4;
// Gap between adjacent button groups on a page — deliberately larger than
// kRibbonGroupPadding so groups read as distinct clusters, not one strip.
inline constexpr int kRibbonGroupSpacing = 10;
// Width of QFrame#ribbonGroup's QSS border (see stylesheet.cpp) — the frame's
// fixed height must include it on top of padding+content, or the layout ends
// up 2px short of what it needs and silently shrinks the bottom margin only.
inline constexpr int kRibbonGroupBorder = 1;
inline constexpr int kRibbonGroupFrameHeight =
    kRibbonButtonSize + 2 * (kRibbonGroupPadding + kRibbonGroupBorder);
// Margins for the horizontal layout of a ribbon page itself.
inline constexpr int kRibbonPageMarginH = 8;
inline constexpr int kRibbonPageMarginV = 3;
inline constexpr int kRibbonPageHeight = kRibbonGroupFrameHeight + 2 * kRibbonPageMarginV;

// A small Word/Excel-style ribbon: a row of tabs, each swapping in its own
// page (typically a strip of icon buttons) below. Deliberately dumb about
// app behavior — it owns no app logic, MainWindow builds the pages, wires
// their actions, and reacts to tab switches via currentTabChanged(). It does
// own the generic visual chrome (tabs, button groups) shared by every page.
class Ribbon : public QWidget {
    Q_OBJECT

public:
    explicit Ribbon(QWidget* parent = nullptr);

    // Takes ownership of `page`. Returns the new tab's index.
    int addTab(const QString& label, QWidget* page, bool enabled = true, const QString& toolTip = QString());

    // Builds one outlined "ribbonGroup" frame holding one QToolButton per
    // action (via QToolButton::setDefaultAction). For use inside a page
    // passed to addTab(); sized to fit a page built at kRibbonPageHeight.
    static QWidget* createButtonGroup(QWidget* parent, const QList<QAction*>& actions);

signals:
    void currentTabChanged(int index);

private:
    QTabBar* m_tabBar;
    QStackedWidget* m_stack;
};

} // namespace traceview
