// The line-follower's emulated sensors (lab/benchmarks/linefollower/plant/
// lfsensors.h): encoder speeds from quantized tick counts, the line sensor
// array's footprint coverage (centred on a straight line: symmetric and
// peaking in the middle; offset: the peak moves to the side the line is
// on), the segment grid against a brute-force distance on the generated
// lemniscate, and the plant exposing true and measured values with the
// noise preset reproducible from its own seed.

#include <QtTest/QtTest>
#include <algorithm>
#include <cmath>
#include <limits>

#include "lab/benchmarks/linefollower/plant/lfsensors.h"
#include "lab/benchmarks/linefollower/plant/linefollowerplant.h"
#include "lab/benchmarks/linefollower/plant/trackgenerator.h"

using namespace tracelab::lf;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A straight line along world x through y = 0.
Track straightTrack() {
    Track track;
    for (int i = 0; i <= 1000; ++i) {
        track.x.push_back(-5.0 + 0.01 * i);
        track.y.push_back(0.0);
    }
    return track;
}

Eigen::VectorXd state(double x, double y, double theta) {
    Eigen::VectorXd s = Eigen::VectorXd::Zero(LfState::Size);
    s[LfState::X] = x;
    s[LfState::Y] = y;
    s[LfState::Theta] = theta;
    return s;
}

std::vector<double> readings(LineSensorArray& array, const Eigen::VectorXd& x) {
    array.sample(x, {});
    std::vector<double> out;
    for (int i = 0; i < array.outputCount(); ++i) {
        out.push_back(array.trueOutput()[i]);
    }
    return out;
}

int argmax(const std::vector<double>& v) {
    return int(std::max_element(v.begin(), v.end()) - v.begin());
}

double centroid(const std::vector<double>& v) {
    double sum = 0.0;
    double weighted = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        weighted += double(i) * v[i];
    }
    return weighted / sum;
}

}  // namespace

class TestLfSensors : public QObject {
    Q_OBJECT

private slots:
    void encoderQuantization() {
        const double ts = 1.0 / 80.0;
        const int ticks = 1024;
        const double resolution = 2.0 * kPi / (ticks * ts);
        EncoderPair encoders(ticks);
        encoders.configure(ts, 1);
        Eigen::VectorXd x = Eigen::VectorXd::Zero(LfState::Size);
        x[LfState::WheelLeft] = 10.0;
        x[LfState::WheelRight] = -3.3;
        double sumL = 0.0;
        double sumR = 0.0;
        const int steps = 4000;
        for (int k = 0; k < steps; ++k) {
            encoders.sample(x, {});
            for (int w = 0; w < 2; ++w) {
                const double m = encoders.measured()[w];
                const double ratio = m / resolution;
                QVERIFY(std::fabs(ratio - std::round(ratio)) < 1e-9);  // whole ticks
                QVERIFY(std::fabs(m - x[w]) < resolution + 1e-9);
            }
            QCOMPARE(encoders.trueOutput()[0], 10.0);
            sumL += encoders.measured()[0];
            sumR += encoders.measured()[1];
        }
        // The counts telescope: the mean is the true speed to one tick over the run.
        QVERIFY(std::fabs(sumL / steps - 10.0) < resolution / steps * 1.01);
        QVERIFY(std::fabs(sumR / steps + 3.3) < resolution / steps * 1.01);
        QCOMPARE(encoders.count(0), (long long)std::floor(10.0 * steps * ts / (2 * kPi / ticks)));

        EncoderPair ideal(0);
        ideal.configure(ts, 1);
        ideal.sample(x, {});
        QCOMPARE(ideal.measured()[0], 10.0);
    }

    void lineArrayCenteredIsSymmetric() {
        const Track track = straightTrack();
        LineArrayParams params;
        params.closedTrack = false;
        params.adcBits = 0;
        LineSensorArray array(track.x, track.y, params);
        array.configure(0.0125, 1);
        // Heading +x: the sensors are 0.15 m ahead, spread along world y.
        const std::vector<double> r = readings(array, state(0.0, 0.0, 0.0));
        QCOMPARE(int(r.size()), 15);
        for (int i = 0; i < 15; ++i) {
            QVERIFY(std::fabs(r[std::size_t(i)] - r[std::size_t(14 - i)]) < 1e-12);
        }
        QCOMPARE(r[7], 1.0);
        QCOMPARE(argmax(r), 5);  // the first of the plateau: |lateral| + r_f < 0.02 at 5..9
        for (int i = 5; i <= 9; ++i) {
            QCOMPARE(r[std::size_t(i)], 1.0);
        }
        QCOMPARE(r[4], 0.0);  // 0.024 m: the footprint (0.022..0.026) is off the line
        // Across the edge the footprint is covered symmetrically.
        QVERIFY(std::fabs(array.coverage(0.019) + array.coverage(0.021) - 1.0) < 1e-12);
        QVERIFY(array.coverage(0.019) > 0.5 && array.coverage(0.019) < 1.0);
        QCOMPARE(r[0], 0.0);
        QVERIFY(std::fabs(centroid(r) - 7.0) < 1e-9);
        QCOMPARE(array.coverage(0.02), 0.5);
    }

    void lineArrayPeakFollowsTheLine() {
        const Track track = straightTrack();
        LineArrayParams params;
        params.closedTrack = false;
        params.adcBits = 0;
        params.lineHalfWidth = 0.005;
        params.footprintDiameter = 0.008;
        LineSensorArray array(track.x, track.y, params);
        array.configure(0.0125, 1);
        const std::vector<double> centred = readings(array, state(1.0, 0.0, 0.0));
        QCOMPARE(argmax(centred), 7);
        QVERIFY(centred[7] > centred[6]);
        QCOMPARE(centred[6], centred[8]);
        QVERIFY(centred[6] > 0.0);

        // Car 6 mm to its right (world +y for heading +x): the line is on
        // its left, the low sensor indices.
        const std::vector<double> right = readings(array, state(1.0, 0.006, 0.0));
        QCOMPARE(argmax(right), 6);
        QVERIFY(centroid(right) < 7.0);
        const std::vector<double> left = readings(array, state(1.0, -0.006, 0.0));
        QCOMPARE(argmax(left), 8);
        QVERIFY(centroid(left) > 7.0);

        // Default line, offset by one spacing: the profile moves by one sensor.
        LineArrayParams wide;
        wide.closedTrack = false;
        wide.adcBits = 0;
        LineSensorArray wideArray(track.x, track.y, wide);
        wideArray.configure(0.0125, 1);
        const double c = centroid(readings(wideArray, state(1.0, 0.008, 0.0)));
        QVERIFY2(std::fabs(c - 6.0) < 1e-9, qPrintable(QString::number(c)));
        // Heading -x mirrors the array: the same car offset reads on the other side.
        const double mirrored = centroid(readings(wideArray, state(1.0, 0.008, kPi)));
        QVERIFY(std::fabs(mirrored - 8.0) < 1e-9);
        // Off the line altogether.
        const std::vector<double> off = readings(wideArray, state(1.0, 0.5, 0.0));
        QCOMPARE(*std::max_element(off.begin(), off.end()), 0.0);
    }

    void adcQuantizesAndSaturates() {
        const Track track = straightTrack();
        LineArrayParams params;
        params.closedTrack = false;
        LineFollowerSensorParams sp;
        sp.line = params;
        sp.line.adcBits = 10;
        sp.line.reading.whiteSigma = 0.3;  // large: forces saturation
        Track t = track;
        LineFollowerSensors sensors(t, sp, 0.0125);
        const LineSensorArray& array = sensors.lineArray();
        QCOMPARE(array.channel(0).config().quantization, 1.0 / 1023.0);
        QCOMPARE(array.channel(0).config().saturationMin, 0.0);
        QCOMPARE(array.channel(0).config().saturationMax, 1.0);
        CarDynamics car;
        for (int k = 0; k < 100; ++k) {
            sensors.sample(car, {0.0, 0.0, 0.0}, k * 0.0125, k);
            for (double v : sensors.measured().line) {
                QVERIFY(v >= 0.0 && v <= 1.0);
                const double code = v * 1023.0;
                QVERIFY(std::fabs(code - std::round(code)) < 1e-9);
            }
        }
    }

    void gridMatchesBruteForce() {
        TrackParams tp;
        const Track track = generateTrack(tp);
        LineArrayParams params;
        LineSensorArray array(track.x, track.y, params);
        const double margin = params.lineHalfWidth + 0.5 * params.footprintDiameter;
        const int n = int(track.size());
        int checked = 0;
        for (int k = 0; k < 400; ++k) {
            const int i = (k * 7919) % n;
            const double angle = k * 0.37;
            const double radius = (k % 10) * 0.004;
            const Point2 p{track.x[i] + radius * std::cos(angle),
                           track.y[i] + radius * std::sin(angle)};
            double best = std::numeric_limits<double>::infinity();
            for (int s = 0; s < n; ++s) {
                const int j = (s + 1) % n;
                const double dx = track.x[j] - track.x[s];
                const double dy = track.y[j] - track.y[s];
                const double l2 = dx * dx + dy * dy;
                double t = l2 > 0 ? ((p.x - track.x[s]) * dx + (p.y - track.y[s]) * dy) / l2 : 0;
                t = std::clamp(t, 0.0, 1.0);
                best = std::min(best,
                                std::hypot(p.x - track.x[s] - t * dx, p.y - track.y[s] - t * dy));
            }
            if (best <= margin) {
                QVERIFY(std::fabs(array.distanceToLine(p) - best) < 1e-12);
                ++checked;
            }
        }
        QVERIFY(checked > 300);
    }

    void plantExposesTrueAndMeasured() {
        TrackParams tp;
        PlantParams pp;
        LineFollowerPlant ideal(generateTrack(tp), pp);
        QVERIFY(std::isnan(ideal.measured().gyro));
        for (int k = 0; k < 40; ++k) {
            QVERIFY(ideal.step(40.0, 35.0));
            QCOMPARE(ideal.trueSensors().wheelSpeedLeft, ideal.data().wheelSpeedLeft);
            QCOMPARE(ideal.trueSensors().wheelSpeedRight, ideal.data().wheelSpeedRight);
            QCOMPARE(ideal.trueSensors().gyro, ideal.data().omega);
            QCOMPARE(ideal.measured().gyro, ideal.data().omega);  // no noise by default
            QVERIFY(std::fabs(ideal.measured().wheelSpeedLeft - ideal.data().wheelSpeedLeft) < 0.5);
            QCOMPARE(int(ideal.measured().line.size()), 15);
        }

        pp.sensors = noisySensorParams();
        pp.sensors.seed = 5;
        LineFollowerPlant a(generateTrack(tp), pp);
        LineFollowerPlant b(generateTrack(tp), pp);
        pp.sensors.seed = 6;
        LineFollowerPlant c(generateTrack(tp), pp);
        ideal.reset();
        int differ = 0;
        for (int k = 0; k < 40; ++k) {
            a.step(40.0, 35.0);
            b.step(40.0, 35.0);
            c.step(40.0, 35.0);
            ideal.step(40.0, 35.0);
            QCOMPARE(a.measured().gyro, b.measured().gyro);
            QCOMPARE(a.measured().line, b.measured().line);
            QCOMPARE(a.pose().x, ideal.pose().x);  // sensors never touch the dynamics
            differ += a.measured().gyro != c.measured().gyro;
        }
        QCOMPARE(differ, 40);
        a.reset();
        QVERIFY(std::isnan(a.measured().wheelSpeedLeft));
    }
};

QTEST_GUILESS_MAIN(TestLfSensors)
#include "test_lfsensors.moc"
