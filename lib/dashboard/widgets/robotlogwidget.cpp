#include "robotlogwidget.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include "protocol/logentry.h"
#include "robotlogmodel.h"

namespace traceview {

RobotLogWidget::RobotLogWidget(QWidget* parent) : DashboardWidget(parent) {
    m_model = new RobotLogModel(this);

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setWordWrap(false);
    m_table->setMinimumHeight(120);

    // Wipes the log, same placement/behavior as SerialMonitorWidget's own
    // Clear button (right-aligned, flat until hovered).
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
    header->addStretch(1);
    header->addWidget(m_clearButton, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header, 0);
    layout->addWidget(m_table, 1);

    // DashboardCell resizes this widget with a bare setGeometry() (not
    // through a parent layout), so pin the floor to exactly what the layout
    // needs -- same reasoning as SerialMonitorWidget's own setMinimumSize()
    // call.
    setMinimumSize(layout->minimumSize());
}

void RobotLogWidget::appendEntry(quint64 timestampUs, quint32 sourceId, quint32 bootId,
                                 quint32 sequence, quint8 severity, const QString& message) {
    // Only follow new rows down if the view was already at the bottom --
    // scrolling up to read earlier lines must not get yanked back down by
    // the next line arriving.
    QScrollBar* scrollBar = m_table->verticalScrollBar();
    const bool wasAtBottom = scrollBar->value() >= scrollBar->maximum() - 1;

    LogEntry entry;
    entry.timestampUs = timestampUs;
    entry.sourceId = sourceId;
    entry.bootId = bootId;
    entry.sequence = sequence;
    entry.severity = static_cast<LogSeverity>(severity);
    entry.message = message;
    m_model->appendEntry(entry);

    if (wasAtBottom) {
        m_table->scrollToBottom();
    }
}

void RobotLogWidget::clearLog() {
    m_model->clear();
}

}  // namespace traceview
