#include "serialdatarouter.h"

#include "dashboard/dashboardgrid.h"
#include "dashboard/dashboardwidget.h"
#include "serialmanager.h"

namespace traceview {

SerialDataRouter::SerialDataRouter(SerialManager* serialManager, DashboardGrid* grid, QObject* parent)
    : QObject(parent), m_grid(grid) {
    rebuildIndex();

    connect(grid, &DashboardGrid::itemsChanged, this, &SerialDataRouter::rebuildIndex);
    connect(serialManager, &SerialManager::dataReceived, this, &SerialDataRouter::onSerialDataReceived);
    // A fresh open shouldn't let a partial line left over from a previous
    // session (e.g. the port was unplugged mid-frame) bleed into the new
    // one.
    connect(serialManager, &SerialManager::connectionStateChanged, this, [this](bool connected) {
        if (connected) {
            m_assembler.reset();
        }
    });
}

void SerialDataRouter::onSerialDataReceived(const QByteArray& data) {
    const QList<QByteArray> lines = m_assembler.feed(data);
    for (const QByteArray& line : lines) {
        const SerialFrame frame = decodeFrame(line);
        if (!frame.ok) {
            // Malformed lines are the terminal's concern (raw passthrough,
            // Task 10), not the router's -- see docs/PROTOCOL.md.
            continue;
        }
        if (DashboardWidget* widget = m_keyedWidgets.value(frame.id)) {
            widget->onSerialPayload(frame.payload);
        }
    }
}

void SerialDataRouter::rebuildIndex() {
    m_keyedWidgets = m_grid->keyedWidgets();
}

} // namespace traceview
