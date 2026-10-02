#include "simwindow.h"

#include <QCloseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QWheelEvent>
#include <cmath>
#include <cstdio>

namespace tracelab::lf {

namespace {

constexpr int kRepaintMs = 15;
// --fast: simulation time per frame before repainting.
constexpr qint64 kFastBudgetMs = 12;
// Real time: never more than this many ticks per frame (a stalled frame
// does not turn into a burst that stalls the next one).
constexpr long long kMaxTicksPerFrame = 2000;
constexpr qint64 kRealTimeBudgetMs = 30;
// Records kept between two frames (three per tick); older ones would fall
// out of the scene's pose history anyway.
constexpr int kMaxPendingRecords = 3 * 300;

QVector<double> toVector(const std::vector<double>& values) {
    return QVector<double>(values.begin(), values.end());
}

}  // namespace

SimWindow::SimWindow(LineFollowerRunner* runner, const WindowOptions& options, QWidget* parent)
    : QWidget(parent), m_runner(runner), m_options(options) {
    setWindowTitle(QStringLiteral("TraceLab - line follower"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    const LineFollowerPlant& plant = runner->plant();
    m_scene.setParam(QStringLiteral("wheel_distance_m"), plant.params().car.wheelsDistance);
    m_scene.setParam(QStringLiteral("render_radius_m"), plant.params().progress.renderRadius);
    m_scene.setParam(QStringLiteral("perturbation_step"), 5.0);  // config.py key_step

    // sim.track: {"points": [[x, y], ...], "closed": true}, metres.
    QJsonArray points;
    const Track& track = plant.track();
    for (std::size_t i = 0; i < track.size(); ++i) {
        points.append(QJsonArray{track.x[i], track.y[i]});
    }
    m_scene.document(QStringLiteral("track"),
                     QJsonDocument(QJsonObject{{"points", points}, {"closed", true}})
                         .toJson(QJsonDocument::Compact));
    m_scene.document(QStringLiteral("controller"),
                     QByteArray::fromStdString(runner->controllerName()));

    publishTick();  // the start pose, tick 0
    flushToScene();

    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SimWindow::frame);
    if (m_options.screenshotPath.isEmpty()) {
        m_clock.start();
        m_timer.start(kRepaintMs);
    }
    m_paintClock.start();
}

bool SimWindow::simulateOne() {
    if (!m_runner->tick()) {
        end();
        return false;
    }
    ++m_ticks;
    publishTick();
    return true;
}

void SimWindow::frame() {
    if (m_ended) {
        return;
    }
    if (m_options.fast) {
        QElapsedTimer budget;
        budget.start();
        while (budget.elapsed() < kFastBudgetMs && simulateOne()) {
        }
    } else {
        const double fps = m_runner->config().fps;
        const long long due =
            (long long)(m_clock.nsecsElapsed() * 1e-9 * m_options.speed * fps) +
            (long long)m_simulatedAtStart;
        QElapsedTimer budget;
        budget.start();
        long long ran = 0;
        while (m_ticks < due && ran < kMaxTicksPerFrame && budget.elapsed() < kRealTimeBudgetMs &&
               simulateOne()) {
            ++ran;
        }
        if (!m_ended && m_ticks < due) {
            // Behind (the controller is slower than Ts): restart the clock
            // from here instead of catching up later in a burst.
            m_simulatedAtStart = double(m_ticks);
            m_clock.restart();
        }
    }
    flushToScene();
    update();
}

void SimWindow::publishTick() {
    const LineFollowerPlant& plant = m_runner->plant();
    const qint64 tick = plant.tick();
    const quint64 us = quint64(std::llround(plant.time() * 1e6));
    const Pose& pose = plant.pose();

    // sim.preview of this tick (before sim.truth, as the plant publishes it).
    SceneRecord preview;
    preview.timestampUs = us;
    preview.values.insert(QStringLiteral("preview_tick"), double(tick));
    QVector<double> px;
    QVector<double> py;
    for (const CarFramePoint& p : plant.data().futurePoints) {
        px.append(p.x);
        py.append(p.y);
    }
    preview.arrays.insert(QStringLiteral("preview_x"), px);
    preview.arrays.insert(QStringLiteral("preview_y"), py);

    // sim.truth.
    SceneRecord truth;
    truth.timestampUs = us;
    truth.values.insert(QStringLiteral("tick"), double(tick));
    truth.values.insert(QStringLiteral("x"), pose.x);
    truth.values.insert(QStringLiteral("y"), pose.y);
    truth.values.insert(QStringLiteral("theta"), pose.theta);
    truth.values.insert(QStringLiteral("next_point"), double(plant.progress().nextPoint()));
    truth.values.insert(QStringLiteral("coverage"), plant.progress().coverage());
    truth.values.insert(QStringLiteral("t"), plant.time());

    QVector<SceneRecord> records{preview, truth};

    // ctl.diag: the prediction of this tick, in its car frame.
    if (m_runner->hasController() && tick > 0) {
        SceneRecord diag;
        diag.timestampUs = us;
        diag.values.insert(QStringLiteral("pred_tick"), double(tick));
        if (m_runner->config().controllerType != "gpc") {
            diag.values.insert(QStringLiteral("qps"), double(m_runner->lastQps()));
        }
        diag.values.insert(QStringLiteral("solve_ms"), m_runner->lastSolveMs());
        diag.arrays.insert(QStringLiteral("pred_x"), toVector(m_runner->predictionX()));
        diag.arrays.insert(QStringLiteral("pred_y"), toVector(m_runner->predictionY()));
        records.append(diag);
    }

    for (const SceneRecord& record : records) {
        for (auto it = record.values.cbegin(); it != record.values.cend(); ++it) {
            m_values.insert(it.key(), it.value());
        }
        for (auto it = record.arrays.cbegin(); it != record.arrays.cend(); ++it) {
            m_arrays.insert(it.key(), it.value());
        }
        m_pending.append(record);
    }
    if (m_pending.size() > kMaxPendingRecords) {
        m_pending.erase(m_pending.begin(), m_pending.end() - kMaxPendingRecords);
    }
}

void SimWindow::flushToScene() {
    SceneFrame frame;
    frame.records = std::move(m_pending);
    m_pending.clear();
    frame.values = m_values;
    frame.arrays = m_arrays;
    m_scene.update(frame);
}

void SimWindow::paintEvent(QPaintEvent*) {
    const qint64 now = m_paintClock.elapsed();
    m_paintTimesMs.append(now);
    while (!m_paintTimesMs.isEmpty() && m_paintTimesMs.first() < now - 1000) {
        m_paintTimesMs.removeFirst();
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    SceneView view;
    view.size = size();
    view.zoom = m_zoom;
    view.renderFps = m_paintTimesMs.size();
    m_scene.paint(painter, view);
}

void SimWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        std::printf("Simulation stopped using ESC\n");
        std::fflush(stdout);
        m_runner->stop("esc");
        end();
        return;
    }
    if (const std::optional<SceneAction> action = m_scene.keyAction(event->key())) {
        // sim.disturb, applied from the next step (left +q, right -q).
        m_runner->plant().setPerturbation(action->argument);
        std::printf("Perturbation set to %g\n", action->argument);
        std::fflush(stdout);
        return;
    }
    QWidget::keyPressEvent(event);
}

void SimWindow::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    m_zoom = qBound(0.1, m_zoom * std::pow(1.15, steps), 20.0);
    update();
}

void SimWindow::closeEvent(QCloseEvent* event) {
    if (m_runner->running()) {
        std::printf("Simulation stopped using X button\n");
        std::fflush(stdout);
        m_runner->stop("quit");
    }
    m_timer.stop();
    m_ended = true;
    QWidget::closeEvent(event);
    emit finished();
}

void SimWindow::end() {
    if (m_ended) {
        return;
    }
    m_ended = true;
    m_timer.stop();
    if (m_runner->stopReason() == "finished") {
        std::printf("Congratulations!\nYou win the game, you score is %.2f\n",
                    100.0 * 100.0 / m_runner->plant().time());
        std::fflush(stdout);
    }
    if (m_options.screenshotPath.isEmpty()) {
        close();
    }
}

bool SimWindow::takeScreenshot() {
    while (!m_ended && m_runner->plant().time() < m_options.screenshotTime) {
        simulateOne();
    }
    flushToScene();
    // Two paints so the fps readout is not 0.
    grab();
    const QPixmap pixmap = grab();
    if (!pixmap.save(m_options.screenshotPath)) {
        std::fprintf(stderr, "could not save %s\n", qPrintable(m_options.screenshotPath));
        return false;
    }
    std::printf("saved %s\n", qPrintable(m_options.screenshotPath));
    std::fflush(stdout);
    return true;
}

}  // namespace tracelab::lf
