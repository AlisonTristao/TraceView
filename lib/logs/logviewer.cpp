#include "logs/logviewer.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "protocol/logfilereader.h"
#include "protocol/logseverity.h"

namespace traceview {

namespace {
constexpr int kTimestampColumn = 0;
constexpr int kSeverityColumn = 1;
constexpr int kSourceIdColumn = 2;
constexpr int kBootIdColumn = 3;
constexpr int kSequenceColumn = 4;
constexpr int kMessageColumn = 5;
constexpr int kColumnCount = 6;

// Matches the "0x%1" 8-digit uppercase hex convention already used for
// source/boot ids elsewhere (see btpbackend.cpp, deviceconfigdialog.cpp).
QString hex32(quint32 value) { return QStringLiteral("0x%1").arg(value, 8, 16, QChar('0')).toUpper(); }
}  // namespace

LogViewer::LogViewer(QWidget* parent) : QWidget(parent) {
    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Timestamp (\xC2\xB5s)"), tr("Severity"), tr("Source ID"), tr("Boot ID"), tr("Sequence"), tr("Message")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_table);
}

void LogViewer::openFile(const QString& filePath) {
    LogFileReader reader;
    if (!reader.load(filePath)) {
        QMessageBox::warning(this, tr("Open Log File"), reader.lastError());
        return;
    }

    const QVector<LogEntry> entries = reader.entries();
    m_table->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const LogEntry& entry = entries.at(row);
        m_table->setItem(row, kTimestampColumn, new QTableWidgetItem(QString::number(entry.timestampUs)));
        m_table->setItem(row, kSeverityColumn,
                          new QTableWidgetItem(QString::fromLatin1(logSeverityToString(entry.severity))));
        m_table->setItem(row, kSourceIdColumn, new QTableWidgetItem(hex32(entry.sourceId)));
        m_table->setItem(row, kBootIdColumn, new QTableWidgetItem(hex32(entry.bootId)));
        m_table->setItem(row, kSequenceColumn, new QTableWidgetItem(QString::number(entry.sequence)));
        m_table->setItem(row, kMessageColumn, new QTableWidgetItem(entry.message));
    }
    m_table->resizeColumnsToContents();
}

}  // namespace traceview
