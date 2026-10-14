// tracelab-linefollower: the line-follower benchmark run in one process --
// the C++ port of line_follower_simulator (track, car, reach rule) closed
// with the C++ qLPV MPC or GPC and drawn by TraceLab's "Line follower"
// scene. A provisional harness (no BTP yet, see simwindow.h); a CLI tool, so
// nothing here is translated.
//
//   tracelab-linefollower [--controller qlpv|gpc] [--seed 1112]
//       [--track-type lemniscate|circle] [--fps 80] [--horizon 45]
//       [--fast | --speed <x>] [--max-time <s>] [--log run.csv]
//       [--screenshot out.png [--screenshot-time 5]]
//       [--sensor-noise on|off] [--sensor-seed 1] [--controller-input true|measured]
//
// Keys: P / O perturbation +/- 5 %, R back to 0, Esc stops. The metrics of
// runner.metrics() are printed when the run ends.

#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <cstdio>

#include "lfrunner.h"
#include "simwindow.h"

using namespace tracelab::lf;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TraceViewTools"));
    QApplication::setApplicationName(QStringLiteral("tracelab-linefollower"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Line-follower benchmark: the Python simulator's track and car, the C++ "
        "controllers and TraceLab's line-follower scene, in one process."));
    parser.addHelpOption();
    const QCommandLineOption controllerOpt(QStringLiteral("controller"),
                                           QStringLiteral("qlpv (default) or gpc."),
                                           QStringLiteral("type"), QStringLiteral("qlpv"));
    const QCommandLineOption seedOpt(QStringLiteral("seed"),
                                     QStringLiteral("Track seed (default 1112, -1 = random)."),
                                     QStringLiteral("n"), QStringLiteral("1112"));
    const QCommandLineOption trackOpt(QStringLiteral("track-type"),
                                      QStringLiteral("lemniscate (default) or circle."),
                                      QStringLiteral("type"), QStringLiteral("lemniscate"));
    const QCommandLineOption fpsOpt(QStringLiteral("fps"),
                                    QStringLiteral("Sample rate in Hz, Ts = 1/fps (default 80)."),
                                    QStringLiteral("hz"), QStringLiteral("80"));
    const QCommandLineOption horizonOpt(QStringLiteral("horizon"),
                                        QStringLiteral("Prediction horizon N (default 45)."),
                                        QStringLiteral("n"), QStringLiteral("45"));
    const QCommandLineOption spacingOpt(
        QStringLiteral("future-spacing"),
        QStringLiteral("Track points between two reference points (default 3)."),
        QStringLiteral("n"), QStringLiteral("3"));
    const QCommandLineOption fastOpt(QStringLiteral("fast"),
                                     QStringLiteral("Run as fast as possible."));
    const QCommandLineOption speedOpt(QStringLiteral("speed"),
                                      QStringLiteral("Time scale of the real-time run (default 1)."),
                                      QStringLiteral("x"), QStringLiteral("1"));
    const QCommandLineOption maxTimeOpt(
        QStringLiteral("max-time"),
        QStringLiteral("Stop after this simulated time in s (default 0 = end of the track)."),
        QStringLiteral("s"), QStringLiteral("0"));
    const QCommandLineOption logOpt(
        QStringLiteral("log"),
        QStringLiteral("Write the run to this CSV (and <name>_track.csv), run_logger.py style."),
        QStringLiteral("file.csv"));
    const QCommandLineOption shotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Simulate --screenshot-time seconds, save the window and exit."),
        QStringLiteral("png"));
    const QCommandLineOption shotTimeOpt(QStringLiteral("screenshot-time"),
                                         QStringLiteral("Simulated seconds before --screenshot "
                                                        "(default 5)."),
                                         QStringLiteral("s"), QStringLiteral("5"));
    const QCommandLineOption sensorNoiseOpt(
        QStringLiteral("sensor-noise"),
        QStringLiteral("Synthetic sensor noise: on or off (default off; encoder and ADC "
                       "quantization are always there)."),
        QStringLiteral("on|off"), QStringLiteral("off"));
    const QCommandLineOption sensorSeedOpt(
        QStringLiteral("sensor-seed"),
        QStringLiteral("Seed of the sensor noise, independent of the track (default 1)."),
        QStringLiteral("n"), QStringLiteral("1"));
    const QCommandLineOption controllerInputOpt(
        QStringLiteral("controller-input"),
        QStringLiteral("Wheel speeds the controller gets: true (the plant's) or measured (the "
                       "encoders'). Default: measured with --sensor-noise on, true otherwise."),
        QStringLiteral("true|measured"));
    for (const QCommandLineOption* o :
         {&controllerOpt, &seedOpt, &trackOpt, &fpsOpt, &horizonOpt, &spacingOpt, &fastOpt,
          &speedOpt, &maxTimeOpt, &logOpt, &shotOpt, &shotTimeOpt, &sensorNoiseOpt, &sensorSeedOpt,
          &controllerInputOpt}) {
        parser.addOption(*o);
    }
    parser.process(app);

    const auto fail = [](const char* message) {
        std::fprintf(stderr, "tracelab-linefollower: %s\n", message);
        return 2;
    };

    RunnerConfig config;
    bool ok = false;
    config.track.seed = parser.value(seedOpt).toLongLong(&ok);
    if (!ok || config.track.seed < -1) {
        return fail("--seed must be an integer >= -1");
    }
    const QString trackType = parser.value(trackOpt);
    if (trackType == QLatin1String("lemniscate")) {
        config.track.type = TrackType::Lemniscate;
    } else if (trackType == QLatin1String("circle")) {
        config.track.type = TrackType::Circle;
    } else {
        return fail("--track-type must be lemniscate or circle");
    }
    config.controllerType = parser.value(controllerOpt).toStdString();
    if (config.controllerType != "qlpv" && config.controllerType != "gpc") {
        return fail("--controller must be qlpv or gpc");
    }
    config.fps = parser.value(fpsOpt).toInt(&ok);
    if (!ok || config.fps < 1) {
        return fail("--fps must be a positive integer");
    }
    config.horizon = parser.value(horizonOpt).toInt(&ok);
    if (!ok || config.horizon < 2) {
        return fail("--horizon must be an integer >= 2");
    }
    config.plant.progress.futureSpacing = parser.value(spacingOpt).toInt(&ok);
    if (!ok || config.plant.progress.futureSpacing < 1) {
        return fail("--future-spacing must be a positive integer");
    }
    config.maxTime = parser.value(maxTimeOpt).toDouble(&ok);
    if (!ok || config.maxTime < 0) {
        return fail("--max-time must be >= 0");
    }
    const QString sensorNoise = parser.value(sensorNoiseOpt);
    if (sensorNoise != QLatin1String("on") && sensorNoise != QLatin1String("off")) {
        return fail("--sensor-noise must be on or off");
    }
    const bool noisy = sensorNoise == QLatin1String("on");
    if (noisy) {
        config.plant.sensors = noisySensorParams(config.plant.sensors);
    }
    config.plant.sensors.seed = parser.value(sensorSeedOpt).toULongLong(&ok);
    if (!ok) {
        return fail("--sensor-seed must be a non-negative integer");
    }
    const QString controllerInput =
        parser.isSet(controllerInputOpt)
            ? parser.value(controllerInputOpt)
            : (noisy ? QStringLiteral("measured") : QStringLiteral("true"));
    if (controllerInput != QLatin1String("true") && controllerInput != QLatin1String("measured")) {
        return fail("--controller-input must be true or measured");
    }
    config.controllerMeasured = controllerInput == QLatin1String("measured");
    WindowOptions options;
    options.fast = parser.isSet(fastOpt);
    options.speed = parser.value(speedOpt).toDouble(&ok);
    if (!ok || options.speed <= 0) {
        return fail("--speed must be > 0");
    }
    options.screenshotPath = parser.value(shotOpt);
    options.screenshotTime = parser.value(shotTimeOpt).toDouble(&ok);
    if (!ok || options.screenshotTime < 0) {
        return fail("--screenshot-time must be >= 0");
    }

    std::printf("Initializing simulator...\n");
    LineFollowerRunner runner(config);
    std::printf("track: %s, seed %lld (generator seed %u), %zu points\n",
                qPrintable(trackType), (long long)config.track.seed, runner.plant().track().innerSeed,
                runner.plant().track().size());
#ifdef TRACELAB_LF_NO_CONTROLLERS
    std::fprintf(stderr,
                 "warning: built without tracelab_lf_controllers -- the car gets u = 0 "
                 "(the controllers library was not found at configure time)\n");
#else
    if (!runner.hasController()) {
        std::fprintf(stderr, "warning: no controller for type '%s' -- the car gets u = 0\n",
                     config.controllerType.c_str());
    }
#endif
    std::printf("controller: %s\n", runner.controllerName().c_str());
    std::printf("sensors: noise %s, seed %llu, controller input %s\n", qPrintable(sensorNoise),
                (unsigned long long)config.plant.sensors.seed, qPrintable(controllerInput));
    std::fflush(stdout);

    SimWindow window(&runner, options);
    window.resize(1400, 750);  // the pygame MEDIUM screen

    int status = 0;
    if (!options.screenshotPath.isEmpty()) {
        status = window.takeScreenshot() ? 0 : 1;
    } else {
        window.show();
        app.exec();
    }
    if (runner.running()) {
        runner.stop("screenshot");
    }

    std::printf("%s", runner.metricsText().c_str());
    if (parser.isSet(logOpt)) {
        std::string error;
        if (runner.saveLog(parser.value(logOpt).toStdString(), &error)) {
            std::printf("Run saved in %s (%zu steps)\n", qPrintable(parser.value(logOpt)),
                        runner.rows().size());
        } else {
            std::fprintf(stderr, "%s\n", error.c_str());
            status = 1;
        }
    }
    std::fflush(stdout);
    return status;
}
