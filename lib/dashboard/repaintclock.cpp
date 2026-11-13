#include "dashboard/repaintclock.h"

#include <QWidget>
#include <utility>

#include "preferences/appsettings.h"

namespace traceview {

RepaintClock& RepaintClock::instance() {
    static RepaintClock clock;
    return clock;
}

RepaintClock::RepaintClock() {
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    setIntervalMs(AppSettings::instance().repaintIntervalMs());
    connect(&AppSettings::instance(), &AppSettings::dashboardPreferencesChanged, this,
            [this] { setIntervalMs(AppSettings::instance().repaintIntervalMs()); });
    connect(&m_timer, &QTimer::timeout, this, &RepaintClock::tick);
}

void RepaintClock::request(QWidget* widget, std::function<void()> action) {
    if (widget == nullptr) {
        return;
    }
    for (const Pending& pending : m_pending) {
        if (pending.widget == widget) {
            return;
        }
    }
    m_pending.append({widget, std::move(action)});
    if (!m_timer.isActive()) {
        // Due one interval after the last tick, not after this request: data
        // arriving between ticks (a simulation at 80 Hz) would otherwise wait
        // a whole interval on top of the time since the last one, and the
        // widgets repaint at well under the rate the profile asks for.
        const qint64 intervalNs = qint64(m_interval) * 1'000'000;
        const qint64 sinceNs = m_sinceTick.isValid() ? m_sinceTick.nsecsElapsed() : intervalNs;
        const qint64 waitNs = intervalNs - sinceNs;
        m_timer.start(waitNs <= 0 ? 0 : int((waitNs + 999'999) / 1'000'000));
    }
}

int RepaintClock::intervalMs() const {
    return m_interval;
}

void RepaintClock::setIntervalMs(int ms) {
    m_interval = qMax(0, ms);
}

void RepaintClock::tick() {
    m_sinceTick.start();
    const QVector<Pending> pending = std::exchange(m_pending, {});
    for (const Pending& entry : pending) {
        if (!entry.widget) {
            continue;
        }
        if (entry.action) {
            entry.action();
        } else {
            entry.widget->update();
        }
    }
}

}  // namespace traceview
