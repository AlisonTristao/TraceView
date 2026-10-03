#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <functional>

class QWidget;

namespace traceview {

// One repaint tick for every dashboard widget fed by telemetry. A widget with
// new data calls request(this) instead of starting its own timer; on the next
// tick (AppSettings::repaintIntervalMs()) every requested widget gets its
// update() in the same event-loop pass, so the window is composed and flushed
// once per frame instead of once per widget, out of phase.
class RepaintClock : public QObject {
    Q_OBJECT

public:
    static RepaintClock& instance();

    // Schedules one update() of `widget` on the next tick; repeated requests
    // before it are one repaint. With `action`, the tick runs it instead (a
    // widget that renders off-thread starts its render, and repaints only
    // when the frame is back).
    void request(QWidget* widget, std::function<void()> action = {});

    // The tick period; 0 repaints on the next event-loop pass.
    int intervalMs() const;
    void setIntervalMs(int ms);

private:
    RepaintClock();
    void tick();

    QTimer m_timer;
    struct Pending {
        QPointer<QWidget> widget;
        std::function<void()> action;
    };
    QVector<Pending> m_pending;
};

}  // namespace traceview
