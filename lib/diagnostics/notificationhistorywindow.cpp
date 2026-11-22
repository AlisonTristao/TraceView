#include "diagnostics/notificationhistorywindow.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "diagnostics/notificationentry.h"
#include "diagnostics/notificationlog.h"
#include "theme/dialogchrome.h"
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

    QVBoxLayout* layout = DialogChrome::setupRootLayout(this);
    layout->addWidget(new DialogHeader(windowTitle(), QString(), this));

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
    // Clearing drops the whole history; there is no undo.
    DialogChrome::setButtonVariant(clearButton, "danger");
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
    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addLayout(DialogChrome::footer(buttons, {copyButton, clearButton}));

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

    connect(m_log, &NotificationLog::entryAdded, this, &NotificationHistoryWindow::onEntryAdded);
    connect(m_log, &NotificationLog::cleared, this, [this] { m_table->setRowCount(0); });
    connect(m_log, &NotificationLog::removed, this, &NotificationHistoryWindow::onEntryRemoved);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) {
                if (isVisible()) {
                    rebuild();
                } else {
                    m_stale = true;
                }
            });

    rebuild();
}

void NotificationHistoryWindow::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (m_stale) {
        rebuild();
    }
}

void NotificationHistoryWindow::onEntryAdded(const NotificationEntry& entry, bool droppedOldest) {
    if (!isVisible()) {
        m_stale = true;
        return;
    }
    if (droppedOldest) {
        trimToLog();
    }
    if (!passesFilter(entry)) {
        return;
    }
    appendRow(entry);
    m_table->scrollToBottom();
}

void NotificationHistoryWindow::onEntryRemoved(quint64 id) {
    if (!isVisible()) {
        m_stale = true;
        return;
    }
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, kTimeColumn)->data(Qt::UserRole).toULongLong() == id) {
            m_table->removeRow(row);
            return;
        }
    }
}

void NotificationHistoryWindow::trimToLog() {
    // Ids grow with every append, so everything older than the log's oldest
    // entry is gone from it -- and sits at the top of the table.
    if (m_log == nullptr || m_log->entries().isEmpty()) {
        m_table->setRowCount(0);
        return;
    }
    const quint64 oldestKept = m_log->entries().constFirst().id;
    int stale = 0;
    while (stale < m_table->rowCount() &&
           m_table->item(stale, kTimeColumn)->data(Qt::UserRole).toULongLong() < oldestKept) {
        ++stale;
    }
    for (int i = 0; i < stale; ++i) {
        m_table->removeRow(0);
    }
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

    auto* timeItem = new QTableWidgetItem(entry.timestamp.toString(QStringLiteral("HH:mm:ss")));
    timeItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.id));
    m_table->setItem(row, kTimeColumn, timeItem);

    auto* severityItem = new QTableWidgetItem(severityLabel(entry.severity));
    severityItem->setForeground(severityColor(entry.severity));
    m_table->setItem(row, kSeverityColumn, severityItem);

    m_table->setItem(row, kSourceColumn, new QTableWidgetItem(entry.source));
    m_table->setItem(row, kMessageColumn, new QTableWidgetItem(entry.text));
}

void NotificationHistoryWindow::rebuild() {
    m_stale = false;
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
