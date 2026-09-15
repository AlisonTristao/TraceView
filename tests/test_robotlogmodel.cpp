#include <QtTest/QtTest>

#include "dashboard/widgets/robotlogmodel.h"
#include "protocol/logentry.h"

using namespace traceview;

namespace {

LogEntry makeEntry(quint32 sequence, LogSeverity severity = LogSeverity::Info,
                   const QString& message = QStringLiteral("hello")) {
    LogEntry entry;
    entry.timestampUs = 1000 + sequence;
    entry.sourceId = 0x11223344;
    entry.bootId = 0xAABBCCDD;
    entry.sequence = sequence;
    entry.severity = severity;
    entry.message = message;
    return entry;
}

class TestRobotLogModel : public QObject {
    Q_OBJECT

private slots:
    void appendEntryGrowsRowsAndReportsFields();
    void ringDropsOldestPastCapacity();
    void clearEmptiesModel();
    void errorSeverityGetsAForegroundColorInfoDoesNot();
};

void TestRobotLogModel::appendEntryGrowsRowsAndReportsFields() {
    RobotLogModel model;
    model.appendEntry(makeEntry(1, LogSeverity::Warn, QStringLiteral("boot ok")));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.columnCount(), int(RobotLogModel::ColumnCount));
    QCOMPARE(model.data(model.index(0, RobotLogModel::TimestampColumn)).toString(),
            QStringLiteral("1001"));
    QCOMPARE(model.data(model.index(0, RobotLogModel::SeverityColumn)).toString(),
            QStringLiteral("WARN"));
    QCOMPARE(model.data(model.index(0, RobotLogModel::SourceIdColumn)).toString(),
            QStringLiteral("0x11223344"));
    QCOMPARE(model.data(model.index(0, RobotLogModel::BootIdColumn)).toString(),
            QStringLiteral("0xAABBCCDD"));
    QCOMPARE(model.data(model.index(0, RobotLogModel::SequenceColumn)).toString(),
            QStringLiteral("1"));
    QCOMPARE(model.data(model.index(0, RobotLogModel::MessageColumn)).toString(),
            QStringLiteral("boot ok"));
}

void TestRobotLogModel::ringDropsOldestPastCapacity() {
    RobotLogModel model;
    const int extra = 5;
    for (int i = 0; i < RobotLogModel::kCapacity + extra; ++i) {
        model.appendEntry(makeEntry(quint32(i)));
    }

    QCOMPARE(model.rowCount(), RobotLogModel::kCapacity);
    // The oldest survivor is the (extra)-th entry ever appended (0-based),
    // so its sequence is `extra`; the newest is the last one appended.
    QCOMPARE(model.data(model.index(0, RobotLogModel::SequenceColumn)).toString(),
            QString::number(extra));
    QCOMPARE(model.data(model.index(model.rowCount() - 1, RobotLogModel::SequenceColumn)).toString(),
            QString::number(RobotLogModel::kCapacity + extra - 1));
}

void TestRobotLogModel::clearEmptiesModel() {
    RobotLogModel model;
    model.appendEntry(makeEntry(1));
    model.appendEntry(makeEntry(2));
    QCOMPARE(model.rowCount(), 2);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

void TestRobotLogModel::errorSeverityGetsAForegroundColorInfoDoesNot() {
    RobotLogModel model;
    model.appendEntry(makeEntry(1, LogSeverity::Error));
    model.appendEntry(makeEntry(2, LogSeverity::Info));

    const QVariant errorColor =
        model.data(model.index(0, RobotLogModel::MessageColumn), Qt::ForegroundRole);
    const QVariant infoColor =
        model.data(model.index(1, RobotLogModel::MessageColumn), Qt::ForegroundRole);

    QVERIFY(errorColor.isValid());
    QVERIFY(!infoColor.isValid());
}

}  // namespace

QTEST_MAIN(TestRobotLogModel)
#include "test_robotlogmodel.moc"
