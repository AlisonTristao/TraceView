#pragma once
// TEMP (not for commit): per-second timing tally, on only with TRACEVIEW_PERF=<file>.

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QTimer>
#include <QByteArray>
#include <atomic>

namespace perf {

struct Tally {
    qint64 ns = 0;
    qint64 calls = 0;
    qint64 maxNs = 0;
};

struct State {
    QMutex mutex;
    QHash<QByteArray, Tally> tallies;
    bool on = !qEnvironmentVariableIsEmpty("TRACEVIEW_PERF");
    QElapsedTimer wall;
    QElapsedTimer idle;
    bool idling = false;
};

inline State& state() {
    static State s;
    return s;
}

inline bool enabled() {
    return state().on;
}

inline void add(const char* name, qint64 ns) {
    State& s = state();
    if (!s.on) {
        return;
    }
    const QMutexLocker lock(&s.mutex);
    Tally& t = s.tallies[QByteArray(name)];
    t.ns += ns;
    ++t.calls;
    t.maxNs = qMax(t.maxNs, ns);
}

inline void count(const char* name) {
    add(name, 0);
}

inline qint64 nowNs() {
    static QElapsedTimer clock = [] {
        QElapsedTimer c;
        c.start();
        return c;
    }();
    return clock.nsecsElapsed();
}

struct Scope {
    const char* name;
    qint64 start;
    explicit Scope(const char* n) : name(n), start(state().on ? nowNs() : 0) {}
    ~Scope() {
        if (state().on) {
            add(name, nowNs() - start);
        }
    }
};

// Starts the per-second dump and the GUI idle meter (call once, GUI thread).
inline void install() {
    State& s = state();
    if (!s.on) {
        return;
    }
    s.wall.start();
    auto* dispatcher = QAbstractEventDispatcher::instance();
    QObject::connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, [] {
        State& st = state();
        st.idling = true;
        st.idle.start();
    });
    QObject::connect(dispatcher, &QAbstractEventDispatcher::awake, [] {
        State& st = state();
        if (st.idling) {
            st.idling = false;
            add("gui.idle", st.idle.nsecsElapsed());
        }
    });
    auto* timer = new QTimer(QCoreApplication::instance());
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, [] {
        State& st = state();
        QHash<QByteArray, Tally> tallies;
        qint64 wallNs = 0;
        {
            const QMutexLocker lock(&st.mutex);
            tallies.swap(st.tallies);
            wallNs = st.wall.nsecsElapsed();
            st.wall.restart();
        }
        QFile file(qEnvironmentVariable("TRACEVIEW_PERF"));
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return;
        }
        QByteArray out = "--- wall " + QByteArray::number(wallNs / 1e6) + "\n";
        for (auto it = tallies.cbegin(); it != tallies.cend(); ++it) {
            out += it.key() + ": " + QByteArray::number(it->ns / 1e6) + " ms, " +
                   QByteArray::number(it->calls) + " calls, max " +
                   QByteArray::number(it->maxNs / 1e6) + " ms\n";
        }
        file.write(out);
    });
    timer->start();
}

inline std::atomic<qint64> planSentNs{0};
inline std::atomic<qint64> stateSentNs{0};

}  // namespace perf

#define PERF_CAT2(a, b) a##b
#define PERF_CAT(a, b) PERF_CAT2(a, b)
#define PERF_SCOPE(name) const perf::Scope PERF_CAT(perfScope_, __LINE__)(name)
