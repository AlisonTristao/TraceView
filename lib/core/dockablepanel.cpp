#include "dockablepanel.h"

#include <QToolButton>
#include <QVBoxLayout>

#include "dockablepanelheader.h"
#include "ribbonicons.h"
#include "traceview/thememanager.h"

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
    connect(m_header->pinButton(), &QToolButton::toggled, this, &DockablePanel::onPinToggled);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->addWidget(m_header);

    m_bodyLayout = new QVBoxLayout();
    m_mainLayout->addLayout(m_bodyLayout, 1);

    updatePinIcon();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updatePinIcon(); });
}

void DockablePanel::onPinToggled(bool checked) {
    m_pinned = checked;
    updatePinIcon();
    emit pinnedChanged(checked);
}

void DockablePanel::updatePinIcon() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_header->pinButton()->setIcon(
        makePinIcon(m_pinned ? palette.accent : palette.textPrimary, m_pinned));
    m_header->pinButton()->setToolTip(m_pinned
                                          ? tr("Unpin — panel will hide when nothing is selected")
                                          : tr("Pin — keep panel open with nothing selected"));
}

}  // namespace traceview
