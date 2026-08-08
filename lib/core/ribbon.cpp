#include "ribbon.h"

#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace traceview {

Ribbon::Ribbon(QWidget* parent) : QWidget(parent) {
    m_tabBar = new QTabBar(this);
    m_stack = new QStackedWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabBar);
    layout->addWidget(m_stack);

    connect(m_tabBar, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
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

} // namespace traceview
