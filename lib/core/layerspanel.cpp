#include "layerspanel.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "dashboard/dashboardgrid.h"
#include "propertiespanel.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "traceview/thememanager.h"

namespace traceview {

LayersPanel::LayersPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(kPropertiesPanelWidth / 3);

    m_pinButton = new QToolButton(this);
    m_pinButton->setObjectName("pinButton");
    m_pinButton->setCheckable(true);
    m_pinButton->setAutoRaise(true);
    m_pinButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_pinButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    connect(m_pinButton, &QToolButton::toggled, this, &LayersPanel::onPinToggled);

    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);
    topBar->addStretch(1);
    topBar->addWidget(m_pinButton);

    m_list = new QListWidget(this);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(topBar);
    mainLayout->addWidget(m_list, 1);

    updatePinIcon();

    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) { onCurrentItemChanged(current); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updatePinIcon(); });
}

void LayersPanel::setItems(const QVector<DashboardLayerEntry>& entries, const QString& selectedId) {
    m_syncing = true;

    m_list->clear();
    // entries is back-to-front (DashboardGrid::layerEntries()); a layers
    // list reads front-most first, so walk it in reverse.
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        auto* row = new QListWidgetItem(it->displayName, m_list);
        row->setData(Qt::UserRole, it->id);
        if (it->id == selectedId) {
            m_list->setCurrentItem(row);
        }
    }
    if (selectedId.isEmpty()) {
        m_list->setCurrentItem(nullptr);
    }

    m_syncing = false;
}

void LayersPanel::onCurrentItemChanged(QListWidgetItem* current) {
    if (m_syncing) {
        return;
    }
    emit itemSelected(current ? current->data(Qt::UserRole).toString() : QString());
}

void LayersPanel::onPinToggled(bool checked) {
    m_pinned = checked;
    updatePinIcon();
    emit pinnedChanged(checked);
}

void LayersPanel::updatePinIcon() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_pinButton->setIcon(makePinIcon(m_pinned ? palette.accent : palette.textPrimary, m_pinned));
    m_pinButton->setToolTip(m_pinned ? "Unpin — panel will hide when nothing is selected"
                                      : "Pin — keep panel open with nothing selected");
}

} // namespace traceview
