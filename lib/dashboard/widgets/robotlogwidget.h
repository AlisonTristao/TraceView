#pragma once

#include <QString>
#include <QtGlobal>

#include "dashboard/dashboardwidget.h"

class QTableView;
class QToolButton;

namespace traceview {

class RobotLogModel;

// A read-only, live "serial monitor" for one device's LOG channel: the same
// look-and-feel contract as SerialMonitorWidget (a thin header row with a
// right-aligned Clear button over the live surface, see
// widgets/serialmonitorwidget.h) but no tab strip -- one widget is always
// exactly one device's log (its config's "deviceId", see
// RobotLogConfigEditor) -- and no input line: LOG has no request/reply half
// to type into, only firmware output. Structured rows (timestamp/severity/
// source/boot/sequence/message) go into a QTableView/RobotLogModel rather
// than SerialTerminalWidget's scrolling text, since every field already
// arrives as its own value rather than an opaque byte stream.
// core/serialwidgetbridge.h resolves the configured device and forwards its
// Backend::logReceived() to appendEntry().
class RobotLogWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit RobotLogWidget(QWidget* parent = nullptr);

public slots:
    // One LOG record. `severity` is the wire's raw octet (traceview::
    // LogSeverity, protocol/logseverity.h) -- see Backend::logReceived().
    void appendEntry(quint64 timestampUs, quint32 sourceId, quint32 bootId, quint32 sequence,
                     quint8 severity, const QString& message);

    void clearLog();

private:
    RobotLogModel* m_model = nullptr;
    QTableView* m_table = nullptr;
    QToolButton* m_clearButton = nullptr;
};

}  // namespace traceview
