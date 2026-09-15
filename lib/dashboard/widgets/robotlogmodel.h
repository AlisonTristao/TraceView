#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "protocol/logentry.h"

namespace traceview {

// Table model backing RobotLogWidget: one already-reassembled LOG record
// (Backend::logReceived) per row, oldest first -- same column layout
// logs/logviewer.h already uses for an offline .blog file (Timestamp/
// Severity/Source ID/Boot ID/Sequence/Message), so the live and offline
// views read the same way.
//
// Bounded at kCapacity (oldest dropped first) via incremental begin/
// endRemoveRows + begin/endInsertRows, the same ring shape as
// diagnostics/framelog.h -- a live LOG stream can run for a whole session,
// and a QTableView backed by a model that only ever grows would eventually
// make every append (and the view's own row-height bookkeeping) slower for
// no benefit, since nobody scrolls back through tens of thousands of lines
// in a dashboard tile. Unlike diagnostics/btpmonitortab.cpp's FrameTableModel
// there is no separate "all entries" + "visible indices" split: one widget
// instance is already scoped to one device (see RobotLogConfigEditor), so
// there is nothing here to filter.
class RobotLogModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        TimestampColumn = 0,
        SeverityColumn,
        SourceIdColumn,
        BootIdColumn,
        SequenceColumn,
        MessageColumn,
        ColumnCount
    };

    static constexpr int kCapacity = 5000;

    explicit RobotLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void appendEntry(const LogEntry& entry);
    void clear();

private:
    QVector<LogEntry> m_entries;
};

}  // namespace traceview
