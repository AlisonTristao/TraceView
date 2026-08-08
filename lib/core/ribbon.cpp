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
}

int Ribbon::addTab(const QString& label, QWidget* page, bool enabled, const QString& toolTip) {
    m_stack->addWidget(page);
    const int index = m_tabBar->addTab(label);
    m_tabBar->setTabEnabled(index, enabled);
    if (!toolTip.isEmpty()) {
        m_tabBar->setTabToolTip(index, toolTip);
    }
    return index;
}

QWidget* Ribbon::createButtonGroup(QWidget* parent, const QList<QAction*>& actions) {
    auto* frame = new QFrame(parent);
    frame->setObjectName("ribbonGroup");
    frame->setFixedHeight(kRibbonGroupFrameHeight);
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(kRibbonGroupPadding, kRibbonGroupPadding, kRibbonGroupPadding, kRibbonGroupPadding);
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

} // namespace traceview
