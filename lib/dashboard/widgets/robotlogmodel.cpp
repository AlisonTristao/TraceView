#include "robotlogmodel.h"

#include <QColor>

#include "dashboard/widgetconfigeditor.h"
#include "protocol/logseverity.h"
#include "traceview/theme.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// Mirrors SerialTerminalWidget's ANSI-role-to-palette mapping (colorForRole
// in serialterminalwidget.cpp): a small, fixed set of ThemePalette tokens,
// never literal RGB, so a theme switch re-tints every row already shown.
// An invalid QColor means "inherit the view's normal text color".
QColor colorForSeverity(LogSeverity severity, const ThemePalette& palette) {
    switch (severity) {
        case LogSeverity::Error:
            return palette.danger;
        case LogSeverity::Warn:
            return palette.warning;
        case LogSeverity::Command:
            return palette.accent;
        case LogSeverity::Debug:
            return palette.textDisabled;
        case LogSeverity::None:
            return palette.textSecondary;
        case LogSeverity::Info:
        default:
            return QColor();
    }
}

}  // namespace

RobotLogModel::RobotLogModel(QObject* parent) : QAbstractTableModel(parent) {
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] {
        if (!m_entries.isEmpty()) {
            emit dataChanged(index(0, SeverityColumn), index(rowCount() - 1, MessageColumn),
                             {Qt::ForegroundRole});
        }
    });
}

int RobotLogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

int RobotLogModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RobotLogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const LogEntry& entry = m_entries.at(index.row());

    if (role == Qt::ForegroundRole) {
        const QColor color = colorForSeverity(entry.severity, ThemeManager::instance().currentTheme());
        return color.isValid() ? QVariant(color) : QVariant();
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
        case TimestampColumn:
            return QString::number(entry.timestampUs);
        case SeverityColumn:
            return QString::fromLatin1(logSeverityToString(entry.severity));
        case SourceIdColumn:
            return formatHexId(entry.sourceId, 8);
        case BootIdColumn:
            return formatHexId(entry.bootId, 8);
        case SequenceColumn:
            return QString::number(entry.sequence);
        case MessageColumn:
            return entry.message;
        default:
            return {};
    }
}

QVariant RobotLogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
        case TimestampColumn:
            return tr("Timestamp (\xC2\xB5s)");
        case SeverityColumn:
            return tr("Severity");
        case SourceIdColumn:
            return tr("Source ID");
        case BootIdColumn:
            return tr("Boot ID");
        case SequenceColumn:
            return tr("Sequence");
        case MessageColumn:
            return tr("Message");
        default:
            return {};
    }
}

void RobotLogModel::appendEntry(const LogEntry& entry) {
    if (m_entries.size() >= kCapacity) {
        beginRemoveRows({}, 0, 0);
        m_entries.removeFirst();
        endRemoveRows();
    }
    const int row = m_entries.size();
    beginInsertRows({}, row, row);
    m_entries.append(entry);
    endInsertRows();
}

void RobotLogModel::clear() {
    if (m_entries.isEmpty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

}  // namespace traceview
