#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>

#include "serialprotocol.h"

namespace traceview {

class DashboardGrid;
class DashboardWidget;
class SerialManager;

// Glue between the frame decoder (Task 4, serialprotocol.h) and the
// dashboard grid: decodes bytes off SerialManager::dataReceived into frames
// and delivers each valid frame's payload to whichever widget's
// DashboardItem::key matches the frame's <id> (DashboardWidget::
// onSerialPayload -- see BACKEND_TODO.txt Task 5/6). SerialManager stays
// bytes-only and DashboardGrid stays serial-agnostic; this is the only
// module that knows about both.
class SerialDataRouter : public QObject {
    Q_OBJECT

public:
    SerialDataRouter(SerialManager* serialManager, DashboardGrid* grid, QObject* parent = nullptr);

public slots:
    // Connected to SerialManager::dataReceived. Also safe to call directly
    // (e.g. in tests) to simulate incoming bytes without a real open port.
    void onSerialDataReceived(const QByteArray& data);

private slots:
    // Connected to DashboardGrid::itemsChanged -- rebuilds m_keyedWidgets so
    // key edits/add/remove/type changes never leave the routing table
    // pointing at stale or wrong widgets.
    void rebuildIndex();

private:
    DashboardGrid* m_grid;
    SerialLineAssembler m_assembler;
    QMap<QString, DashboardWidget*> m_keyedWidgets;
};

} // namespace traceview
