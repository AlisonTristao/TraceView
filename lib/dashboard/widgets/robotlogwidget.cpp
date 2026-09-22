#include "robotlogwidget.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include "protocol/logentry.h"
#include "robotlogmodel.h"
#include "terminaltabbar.h"

namespace traceview {

RobotLogWidget::RobotLogWidget(QWidget* parent) : DashboardWidget(parent) {
    m_tabBar = new TerminalTabBar(this);
    m_tabBar->hide();

    m_stack = new QStackedWidget(this);
    m_stack->setMinimumHeight(120);

    // Wipes the visible tab's log, same placement/behavior as
    // SerialMonitorWidget's own Clear button.
    m_clearButton = new QToolButton(this);
    m_clearButton->setText(tr("Clear"));
    m_clearButton->setToolTip(tr("Clear this log"));
    m_clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    connect(m_clearButton, &QToolButton::clicked, this, &RobotLogWidget::clearLog);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(0);
    header->addWidget(m_tabBar, 0);
    header->addStretch(1);
    header->addWidget(m_clearButton, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header, 0);
    layout->addWidget(m_stack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) { showTab(index); });

    // One empty tab until the real config arrives -- keeps the widget in a
    // valid "single table, no tab strip" state if it's ever shown first.
    rebuildTabs({QString()});

    // DashboardCell resizes this widget with a bare setGeometry() (not
    // through a parent layout), so pin the floor to exactly what the layout
    // needs -- same reasoning as SerialMonitorWidget's own setMinimumSize().
    setMinimumSize(layout->minimumSize());
}

void RobotLogWidget::setConfig(const QJsonObject& config) {
    QStringList deviceIds;
    if (config.value("tabs").isArray()) {
        const QJsonArray tabs = config.value("tabs").toArray();
        for (const QJsonValue& tab : tabs) {
            deviceIds.append(tab.toObject().value("deviceId").toString());
        }
    } else {
        // Pre-tabs config: a single tab from the old flat "deviceId" (the
        // only shape this widget shipped with before tabs existed).
        deviceIds.append(config.value("deviceId").toString());
    }
    if (deviceIds.isEmpty()) {
        deviceIds.append(QString());
    }
    if (deviceIds == m_deviceIds) {
        return;
    }

    rebuildTabs(deviceIds);
    emit tabsChanged();
}

void RobotLogWidget::setDeviceNames(const QHash<QString, QString>& namesById) {
    m_deviceNames = namesById;
    refreshTabLabels();
}

void RobotLogWidget::feedDevice(const QString& deviceId, quint64 timestampUs, quint32 sourceId,
                                quint32 bootId, quint32 sequence, quint8 severity,
                                const QString& message) {
    if (deviceId.isEmpty()) {
        return;
    }

    LogEntry entry;
    entry.timestampUs = timestampUs;
    entry.sourceId = sourceId;
    entry.bootId = bootId;
    entry.sequence = sequence;
    entry.severity = static_cast<LogSeverity>(severity);
    entry.message = message;

    for (int i = 0; i < m_models.size(); ++i) {
        if (m_deviceIds.value(i) != deviceId) {
            continue;
        }
        // Only follow new rows down if this tab was already scrolled to the
        // bottom -- same reasoning as the single-device version this
        // replaces: reading earlier lines must not get yanked down by the
        // next one arriving, whether or not this tab is even visible.
        QTableView* table = m_tables[i];
        QScrollBar* scrollBar = table->verticalScrollBar();
        const bool wasAtBottom = scrollBar->value() >= scrollBar->maximum() - 1;

        m_models[i]->appendEntry(entry);

        if (wasAtBottom) {
            table->scrollToBottom();
        }
    }
}

void RobotLogWidget::clearLog() {
    const int index = m_stack->currentIndex();
    if (index >= 0 && index < m_models.size()) {
        m_models[index]->clear();
    }
}

void RobotLogWidget::rebuildTabs(const QStringList& deviceIds) {
    for (QTableView* table : m_tables) {
        delete table;  // also drops it from m_stack and deletes its model (parented to it)
    }
    m_tables.clear();
    m_models.clear();
    while (m_tabBar->count() > 0) {
        m_tabBar->removeTab(0);
    }

    m_deviceIds = deviceIds;

    for (int i = 0; i < deviceIds.size(); ++i) {
        auto* table = new QTableView(this);
        auto* model = new RobotLogModel(table);
        table->setModel(model);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setWordWrap(false);
        table->setMinimumHeight(120);
        // Boot ID and Sequence are mostly noise for a live dashboard tile --
        // hidden rather than removed from the model, so Message (the
        // stretched last section) picks up their freed width automatically.
        table->hideColumn(RobotLogModel::BootIdColumn);
        table->hideColumn(RobotLogModel::SequenceColumn);

        m_models.append(model);
        m_tables.append(table);
        m_stack->addWidget(table);
        m_tabBar->addTab(labelFor(deviceIds[i]));
    }

    m_tabBar->setVisible(m_tables.size() >= 2);
    if (!m_tables.isEmpty()) {
        showTab(0);
    }
}

void RobotLogWidget::showTab(int index) {
    if (index < 0 || index >= m_tables.size()) {
        return;
    }
    if (m_tabBar->currentIndex() != index) {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->setCurrentIndex(index);
    }
    m_stack->setCurrentIndex(index);
}

QString RobotLogWidget::labelFor(const QString& deviceId) const {
    if (deviceId.isEmpty()) {
        return tr("(no device)");
    }
    const QString name = m_deviceNames.value(deviceId);
    return name.isEmpty() ? tr("(unnamed device)") : name;
}

void RobotLogWidget::refreshTabLabels() {
    for (int i = 0; i < m_tables.size(); ++i) {
        m_tabBar->setTabText(i, labelFor(m_deviceIds.value(i)));
    }
}

}  // namespace traceview
