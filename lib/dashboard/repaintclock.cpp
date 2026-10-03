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
    m_timer.setInterval(AppSettings::instance().repaintIntervalMs());
    connect(&AppSettings::instance(), &AppSettings::dashboardPreferencesChanged, this,
            [this] { m_timer.setInterval(AppSettings::instance().repaintIntervalMs()); });
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
        m_timer.start();
    }
}

int RepaintClock::intervalMs() const {
    return m_timer.interval();
}

void RepaintClock::setIntervalMs(int ms) {
    m_timer.setInterval(qMax(0, ms));
}

void RepaintClock::tick() {
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
