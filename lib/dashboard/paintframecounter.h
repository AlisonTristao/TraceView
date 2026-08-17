#pragma once

#include <QAtomicInteger>
#include <QtGlobal>

namespace traceview {

// Debug-only global tally of dashboard widget repaints (chart/bar/gauge
// paintEvent calls), read by MainWindow's Debug > Show statistics FPS
// readout (see mainwindow.cpp). Nothing here computes a rate itself -- the
// reader samples the running total once a second and diffs it.
inline QAtomicInteger<quint64>& paintFrameCounter() {
    static QAtomicInteger<quint64> counter{0};
    return counter;
}

inline void notePaintFrame() {
    paintFrameCounter().fetchAndAddRelaxed(1);
}

}  // namespace traceview
