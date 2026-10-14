#ifndef TRACELAB_LF_SIMWINDOW_H
#define TRACELAB_LF_SIMWINDOW_H

// tracelab-linefollower's window: the DiffDriveScene ("Line follower") fed
// DIRECTLY from the in-process runner.
//
// PROVISIONAL HARNESS: there is no BTP here. The plan (lab/README.md) is a
// SimulatedPlant and a ControllerHost talking the loop profile over the
// loopback transport, with the Scene widget bound to sim.truth, sim.preview,
// ctl.diag... by name. Until that exists this window builds the same
// SceneFrame records those topics would carry (same input names, same tick
// keys) and hands them to the scene itself.
//
// The simulation runs at a fixed step Ts = 1 / fps, decoupled from the
// repaint timer: in real time (scaled by --speed) the number of ticks due is
// taken from a monotonic clock; with --fast as many ticks as fit in a frame
// budget run between two repaints. When the controller is slower than Ts
// the real-time run falls back to slow motion (the clock is re-anchored)
// rather than freezing the window to catch up.

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "lab/scenes/diffdrivescene.h"
#include "lfrunner.h"

namespace tracelab::lf {

struct WindowOptions {
    bool fast = false;
    double speed = 1.0;  // time scale in real time
    // > 0: run this many simulated seconds as fast as possible, save the
    // window to `screenshotPath` and close.
    QString screenshotPath;
    double screenshotTime = 5.0;
};

class SimWindow : public QWidget {
    Q_OBJECT
public:
    SimWindow(LineFollowerRunner* runner, const WindowOptions& options, QWidget* parent = nullptr);

    // Runs the screenshot mode synchronously; false if saving failed.
    bool takeScreenshot();

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void frame();
    bool simulateOne();
    void publishTick();
    void flushToScene();
    void end();

    LineFollowerRunner* m_runner;
    WindowOptions m_options;
    tracelab::DiffDriveScene m_scene;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_simulatedAtStart = 0.0;  // ticks done when the clock started
    long long m_ticks = 0;
    bool m_ended = false;

    QVector<tracelab::SceneRecord> m_pending;
    QHash<QString, double> m_values;
    QHash<QString, QVector<double>> m_arrays;
    double m_zoom = 1.0;
    QVector<qint64> m_paintTimesMs;
    QElapsedTimer m_paintClock;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_SIMWINDOW_H
