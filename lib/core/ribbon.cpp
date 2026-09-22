#include "ribbon.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QSize>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ribbonicons.h"
#include "ribbontabbar.h"

namespace traceview {

Ribbon::Ribbon(QWidget* parent) : QWidget(parent) {
    setObjectName("ribbon");

    m_tabBar = new RibbonTabBar(this);
    m_stack = new QStackedWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // The tabs share their row with whatever setTabBarCornerWidget() parks
    // at the right end, so the row -- not m_tabBar directly -- is what goes
    // into the column here. Zero margins/spacing keep it visually identical
    // to m_tabBar having been added on its own.
    m_tabRow = new QWidget(this);
    m_tabRowLayout = new QHBoxLayout(m_tabRow);
    m_tabRowLayout->setContentsMargins(0, 0, 0, 0);
    m_tabRowLayout->setSpacing(0);
    // Give the bar all space left by the corner control. A separate stretch
    // competes with the bar and can trigger scrolling while the row has room.
    m_tabRowLayout->addWidget(m_tabBar, /*stretch=*/1);

    layout->addWidget(m_tabRow);
    layout->addWidget(m_stack);

    connect(m_tabBar, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_tabBar, &QTabBar::currentChanged, this, &Ribbon::currentTabChanged);

    // QTabBar::currentChanged only fires on an actual index change, so
    // re-clicking the tab that's already current is normally a no-op. That
    // stops being harmless now that MainWindow can swap its content area
    // away from the ribbon entirely (File > Open Log Offline) without
    // touching this tab bar's current index -- without this, clicking back
    // on the tab that was active before opening a log would do nothing.
    connect(m_tabBar, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index == m_tabBar->currentIndex()) {
            emit currentTabChanged(index);
        }
    });
    connect(m_tabBar, &RibbonTabBar::tabCloseRequested, this, &Ribbon::tabCloseRequested);
}

int Ribbon::addTab(const QString& label, QWidget* page, bool enabled, const QString& toolTip,
                   bool closable) {
    m_stack->addWidget(page);
    const int index = m_tabBar->addTab(label);
    m_tabBar->setTabEnabled(index, enabled);
    if (!toolTip.isEmpty()) {
        m_tabBar->setTabToolTip(index, toolTip);
    }
    m_tabBar->setTabClosable(index, closable);
    return index;
}

void Ribbon::removeTab(int index) {
    // Stack shrinks first, tab bar last: the tab bar's currentChanged is
    // what drives both m_stack's own setCurrentIndex (via the constructor's
    // connection) and, through Ribbon::currentTabChanged, MainWindow's own
    // content swap -- both need m_stack already down to its final N-1 pages
    // by the time that signal goes out, or "the new current tab's index"
    // would momentarily resolve against the old, longer stack.
    QWidget* page = m_stack->widget(index);
    m_stack->removeWidget(page);
    delete page;
    m_tabBar->removeTab(index);
}

QWidget* Ribbon::pageAt(int index) const {
    return m_stack->widget(index);
}

int Ribbon::count() const {
    return m_stack->count();
}

void Ribbon::setCurrentIndex(int index) {
    m_tabBar->setCurrentIndex(index);
}

int Ribbon::currentIndex() const {
    return m_tabBar->currentIndex();
}

void Ribbon::setTabBarVisible(bool visible) {
    // The whole row, so a corner widget goes down with the tabs -- callers
    // mean "hide the strip", and leaving a lone button floating where the
    // tabs were is not that.
    m_tabRow->setVisible(visible);
}

void Ribbon::setTabBarCornerWidget(QWidget* widget) {
    if (m_tabBarCornerWidget == widget) {
        return;
    }
    if (m_tabBarCornerWidget) {
        m_tabRowLayout->removeWidget(m_tabBarCornerWidget);
    }
    m_tabBarCornerWidget = widget;
    if (m_tabBarCornerWidget) {
        // The tab bar absorbs the remaining width, keeping this at the right edge.
        m_tabRowLayout->addWidget(m_tabBarCornerWidget);
    }
}

void Ribbon::setTabVisible(int index, bool visible) {
    m_tabBar->setTabVisible(index, visible);
}

QWidget* Ribbon::createButtonGroup(QWidget* parent, const QList<QAction*>& actions) {
    auto* frame = new QFrame(parent);
    frame->setObjectName("ribbonGroup");
    frame->setFixedHeight(kRibbonGroupFrameHeight);
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(kRibbonGroupPadding, kRibbonGroupPadding, kRibbonGroupPadding,
                               kRibbonGroupPadding);
    layout->setSpacing(kRibbonGroupPadding);

    for (QAction* action : actions) {
        auto* button = new QToolButton(frame);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
        button->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
        layout->addWidget(button);
    }

    return frame;
}

}  // namespace traceview
