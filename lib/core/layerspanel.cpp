#include "layerspanel.h"

#include <QListWidget>
#include <QVBoxLayout>

#include "dashboard/dashboardgrid.h"
#include "propertiespanel.h"

namespace traceview {

LayersPanel::LayersPanel(QWidget* parent) : DockablePanel(parent) {
    setObjectName("layersPanel");
    mainLayout()->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(this);
    bodyLayout()->addWidget(m_list);

    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) { onCurrentItemChanged(current); });
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

int LayersPanel::preferredThickness() const {
    return kPropertiesPanelWidth / 3;
}

void LayersPanel::onCurrentItemChanged(QListWidgetItem* current) {
    if (m_syncing) {
        return;
    }
    emit itemSelected(current ? current->data(Qt::UserRole).toString() : QString());
}

}  // namespace traceview
