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
    layout->addWidget(m_tabBar);
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

void Ribbon::setTabBarVisible(bool visible) {
    m_tabBar->setVisible(visible);
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
