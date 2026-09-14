#include "dockablepanel.h"

#include <QVBoxLayout>

#include "dockablepanelheader.h"

namespace traceview {

DockablePanel::DockablePanel(QWidget* parent) : QWidget(parent) {
    // Qt only auto-paints a QSS `background-color` for plain QWidget
    // instances, not subclasses (see DashboardWidget's constructor for the
    // same fix/rationale) -- without this, any part of the panel not covered
    // edge-to-edge by a child widget leaks whatever's underneath through
    // instead of showing the panel's own fill (see stylesheet.cpp's
    // "QWidget#layersPanel"/"QWidget#propertiesPanel" rules, keyed off the
    // objectName each subclass sets itself).
    setAttribute(Qt::WA_StyledBackground, true);

    m_header = new DockablePanelHeader(this);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->addWidget(m_header);

    m_bodyLayout = new QVBoxLayout();
    m_mainLayout->addLayout(m_bodyLayout, 1);
}

}  // namespace traceview
