#include "diagnostics/notificationhistorywindow.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "diagnostics/notificationentry.h"
#include "diagnostics/notificationlog.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kTimeColumn = 0;
constexpr int kSeverityColumn = 1;
constexpr int kSourceColumn = 2;
constexpr int kMessageColumn = 3;
constexpr int kColumnCount = 4;

QString severityLabel(StatusSeverity severity) {
    switch (severity) {
        case StatusSeverity::Info:
            return QObject::tr("Info");
        case StatusSeverity::Success:
            return QObject::tr("Success");
        case StatusSeverity::Warning:
            return QObject::tr("Warning");
        case StatusSeverity::Error:
            return QObject::tr("Error");
    }
    return QObject::tr("Info");
}

QColor severityColor(StatusSeverity severity) {
    const ThemePalette& theme = ThemeManager::instance().currentTheme();
    switch (severity) {
        case StatusSeverity::Success:
            return theme.success;
        case StatusSeverity::Warning:
            return theme.warning;
        case StatusSeverity::Error:
            return theme.danger;
        case StatusSeverity::Info:
            break;
    }
    return theme.textSecondary;
}
}  // namespace

NotificationHistoryWindow::NotificationHistoryWindow(NotificationLog* log, QWidget* parent)
    : QDialog(parent), m_log(log) {
    setWindowTitle(tr("Notification History"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 440);

    auto* layout = new QVBoxLayout(this);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Severity:"), this));
    m_severityFilter = new QComboBox(this);
    m_severityFilter->addItem(tr("All"), QVariant());
    m_severityFilter->addItem(severityLabel(StatusSeverity::Info),
                              int(StatusSeverity::Info));
    m_severityFilter->addItem(severityLabel(StatusSeverity::Success),
                              int(StatusSeverity::Success));
    m_severityFilter->addItem(severityLabel(StatusSeverity::Warning),
                              int(StatusSeverity::Warning));
    m_severityFilter->addItem(severityLabel(StatusSeverity::Error),
                              int(StatusSeverity::Error));
    controls->addWidget(m_severityFilter);
    controls->addStretch();

    auto* copyButton = new QPushButton(tr("Copy"), this);
    auto* clearButton = new QPushButton(tr("Clear"), this);
    controls->addWidget(copyButton);
    controls->addWidget(clearButton);
    layout->addLayout(controls);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Time"), tr("Severity"), tr("Source"), tr("Message")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(false);
    layout->addWidget(m_table);

    connect(m_severityFilter, &QComboBox::currentIndexChanged, this,
            &NotificationHistoryWindow::rebuild);
    connect(clearButton, &QPushButton::clicked, m_log, &NotificationLog::clear);
    connect(copyButton, &QPushButton::clicked, this, [this] {
        QString text;
        const bool onlySelected = !m_table->selectedItems().isEmpty();
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (onlySelected && !m_table->item(row, kTimeColumn)->isSelected()) {
                continue;
            }
            text += QStringLiteral("%1\t%2\t%3\t%4\n")
                        .arg(m_table->item(row, kTimeColumn)->text(),
                             m_table->item(row, kSeverityColumn)->text(),
                             m_table->item(row, kSourceColumn)->text(),
                             m_table->item(row, kMessageColumn)->text());
        }
        QApplication::clipboard()->setText(text);
    });

    connect(m_log, &NotificationLog::entryAdded, this,
            [this](const NotificationEntry& entry, bool) {
                if (!passesFilter(entry)) {
                    return;
                }
                appendRow(entry);
                m_table->scrollToBottom();
            });
    connect(m_log, &NotificationLog::cleared, this, [this] { m_table->setRowCount(0); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { rebuild(); });

    rebuild();
}

bool NotificationHistoryWindow::passesFilter(const NotificationEntry& entry) const {
    const QVariant data = m_severityFilter->currentData();
    if (!data.isValid()) {
        return true;
    }
    return int(entry.severity) == data.toInt();
}

void NotificationHistoryWindow::appendRow(const NotificationEntry& entry) {
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    m_table->setItem(row, kTimeColumn,
                     new QTableWidgetItem(entry.timestamp.toString(QStringLiteral("HH:mm:ss"))));

    auto* severityItem = new QTableWidgetItem(severityLabel(entry.severity));
    severityItem->setForeground(severityColor(entry.severity));
    m_table->setItem(row, kSeverityColumn, severityItem);

    m_table->setItem(row, kSourceColumn, new QTableWidgetItem(entry.source));
    m_table->setItem(row, kMessageColumn, new QTableWidgetItem(entry.text));
}

void NotificationHistoryWindow::rebuild() {
    m_table->setRowCount(0);
    if (m_log == nullptr) {
        return;
    }
    m_table->setUpdatesEnabled(false);
    for (const NotificationEntry& entry : m_log->entries()) {
        if (passesFilter(entry)) {
            appendRow(entry);
        }
    }
    m_table->setUpdatesEnabled(true);
    m_table->resizeColumnToContents(kTimeColumn);
    m_table->resizeColumnToContents(kSeverityColumn);
    m_table->scrollToBottom();
}

}  // namespace traceview
