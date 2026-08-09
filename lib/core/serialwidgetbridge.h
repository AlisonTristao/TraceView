#pragma once

#include <QObject>

namespace traceview {

class DashboardGrid;
class DashboardWidget;
class SerialManager;

// Wires each control/serial-monitor widget's output to the shared
// SerialManager as soon as it's created (BACKEND_TODO.txt Tasks 9/10):
// PushButtonWidget/ToggleSwitchWidget/SliderWidget's sendRequested() (a
// fully-formed command) go through SerialManager::writeCommand() (appends
// the configured line terminator); SerialMonitorWidget's sendRequested()
// (raw keystrokes) goes through SerialManager::write() untouched, and every
// SerialMonitorWidget also gets SerialManager::dataReceived() forwarded to
// its appendData() so it shows the whole raw stream, independent of
// key-based routing (SerialDataRouter, Task 5, only delivers to the widget
// whose key matches -- the terminal is not addressed, it's a passive tap on
// everything).
//
// Hooks DashboardGrid::widgetCreated() rather than rebuilding an index off
// itemsChanged() the way SerialDataRouter does: outbound wiring has no key
// to go stale, so a one-shot per-instance connect at construction time is
// enough, and avoids double-connecting a widget that persists across an
// unrelated itemsChanged() (e.g. some other item's key edit).
class SerialWidgetBridge : public QObject {
    Q_OBJECT

public:
    SerialWidgetBridge(SerialManager* serialManager, DashboardGrid* grid, QObject* parent = nullptr);

private:
    void wireWidget(DashboardWidget* widget);

    SerialManager* m_serialManager;
};

} // namespace traceview
