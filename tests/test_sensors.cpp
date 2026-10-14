// TraceLab's generic sensor blocks (lab/sensors): the reproducible random
// streams (mt19937_64 as the standard fixes it, own Box-Muller, one stream
// per sensor and per process), the statistics of each imperfection model
// (white noise, random walk, Gauss-Markov), the deterministic ones
// (scale/bias, saturation, quantization, delay, own rate, dropout,
// outliers), and the observation blocks: linear H x + D u and nonlinear
// h(x, u, env) in its three forms, mixed in one SensorSet. Plain C++ under
// test, Qt Test only as the harness.

#include <QtTest/QtTest>
#include <cmath>
#include <vector>

#include "lab/sensors/imperfections.h"
#include "lab/sensors/observation.h"
#include "lab/sensors/sensorrng.h"

using namespace tracelab::sensors;

namespace {

struct Stats {
    double mean = 0.0;
    double variance = 0.0;
};

Stats stats(const std::vector<double>& values) {
    Stats s;
    for (double v : values) {
        s.mean += v;
    }
    s.mean /= double(values.size());
    for (double v : values) {
        s.variance += (v - s.mean) * (v - s.mean);
    }
    s.variance /= double(values.size() - 1);
    return s;
}

std::vector<double> run(const ImperfectionConfig& config, const std::vector<double>& input,
                        double ts = 0.01, std::uint64_t seed = 7) {
    ChannelImperfections channel(config, ts, seed);
    std::vector<double> out;
    for (double v : input) {
        out.push_back(channel.apply(v));
    }
    return out;
}

// Range and bearing of the point (x0, x1) seen from the origin.
Eigen::VectorXd rangeBearing(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                             const SensorEnvironment& env) {
    (void)u;
    (void)env;
    Eigen::VectorXd y(2);
    y << std::sqrt(x[0] * x[0] + x[1] * x[1]), std::atan2(x[1], x[0]);
    return y;
}

void rangeBearingRaw(const double* x, int nx, const double* u, int nu, const SensorEnvironment& env,
                     double* y, void* context) {
    (void)nx;
    (void)u;
    (void)nu;
    (void)env;
    ++*static_cast<int*>(context);
    y[0] = std::sqrt(x[0] * x[0] + x[1] * x[1]);
    y[1] = std::atan2(x[1], x[0]);
}

}  // namespace

class TestSensors : public QObject {
    Q_OBJECT

private slots:
    void engineIsTheStandardOne() {
        // [rand.predef]: the 10000th value of a default-constructed mt19937_64.
        SensorRng rng(5489u);
        std::uint64_t value = 0;
        for (int i = 0; i < 10000; ++i) {
            value = rng.nextU64();
        }
        QCOMPARE(value, std::uint64_t(9981545732273789042ULL));
    }

    void sameSeedSameNumbers() {
        SensorRng a(42);
        SensorRng b(42);
        SensorRng c(43);
        int differ = 0;
        for (int i = 0; i < 1000; ++i) {
            const double va = a.gaussian();
            QCOMPARE(va, b.gaussian());
            differ += va != c.gaussian();
        }
        QVERIFY(differ > 990);
        a.reseed(42);
        b.reseed(42);
        QCOMPARE(a.uniform(), b.uniform());
        QVERIFY(deriveSeed(1, "gyro/0") != deriveSeed(1, "gyro/1"));
        QVERIFY(deriveSeed(1, "gyro/0") != deriveSeed(2, "gyro/0"));
        QCOMPARE(deriveSeed(1, "gyro/0"), deriveSeed(1, "gyro/0"));
    }

    void uniformAndGaussianMoments() {
        SensorRng rng(3);
        std::vector<double> u;
        std::vector<double> g;
        for (int i = 0; i < 200000; ++i) {
            u.push_back(rng.uniform());
            g.push_back(rng.gaussian());
        }
        for (double v : u) {
            QVERIFY(v >= 0.0 && v < 1.0);
        }
        const Stats su = stats(u);
        QVERIFY(std::fabs(su.mean - 0.5) < 0.005);
        QVERIFY(std::fabs(su.variance - 1.0 / 12.0) < 0.002);
        const Stats sg = stats(g);
        QVERIFY(std::fabs(sg.mean) < 0.01);
        QVERIFY(std::fabs(sg.variance - 1.0) < 0.02);
    }

    void perSensorStreamsAreIndependent() {
        // Block "b" sampled alone, then with another block added before it
        // and imperfections switched on in a second channel: same noise.
        ImperfectionConfig noisy;
        noisy.whiteSigma = 1.0;
        noisy.randomWalkSigma = 0.5;
        const auto makeB = [&] {
            auto b = std::make_unique<LinearObservation>("b", Eigen::MatrixXd::Identity(2, 2));
            b->setImperfections(0, noisy);
            return b;
        };
        SensorSet alone;
        SensorBlock& b1 = alone.add(makeB());
        alone.configure(0.01, 99);

        SensorSet more;
        auto a = std::make_unique<LinearObservation>("a", Eigen::MatrixXd::Identity(2, 2));
        a->setAllImperfections(noisy);
        more.add(std::move(a));
        auto b = makeB();
        ImperfectionConfig other;
        other.outlierProbability = 0.5;
        other.outlierMagnitude = 3.0;
        b->setImperfections(1, other);
        SensorBlock& b2 = more.add(std::move(b));
        more.configure(0.01, 99);

        const Eigen::VectorXd x = Eigen::VectorXd::Zero(2);
        int differFromA = 0;
        for (int k = 0; k < 500; ++k) {
            alone.sample(x, {});
            more.sample(x, {});
            QCOMPARE(b1.measured()[0], b2.measured()[0]);
            differFromA += more.at(0).measured()[0] != b2.measured()[0];
        }
        QVERIFY(differFromA > 490);
        // Another master seed changes it.
        more.configure(0.01, 100);
        alone.reset();
        alone.sample(x, {});
        more.sample(x, {});
        QVERIFY(b1.measured()[0] != b2.measured()[0]);
    }

    void resetReplaysTheSameNoise() {
        ImperfectionConfig c;
        c.whiteSigma = 1.0;
        c.gaussMarkovSigma = 1.0;
        c.gaussMarkovTau = 0.1;
        ChannelImperfections channel(c, 0.01, 5);
        std::vector<double> first;
        for (int i = 0; i < 50; ++i) {
            first.push_back(channel.apply(0.0));
        }
        channel.reset();
        for (int i = 0; i < 50; ++i) {
            QCOMPARE(channel.apply(0.0), first[std::size_t(i)]);
        }
    }

    void whiteNoiseMeanAndSigma() {
        ImperfectionConfig c;
        c.whiteSigma = 0.5;
        const std::vector<double> out = run(c, std::vector<double>(200000, 2.0));
        const Stats s = stats(out);
        QVERIFY2(std::fabs(s.mean - 2.0) < 0.01, qPrintable(QString::number(s.mean)));
        QVERIFY2(std::fabs(std::sqrt(s.variance) - 0.5) < 0.01,
                 qPrintable(QString::number(std::sqrt(s.variance))));
    }

    void randomWalkVarianceGrowsLinearly() {
        // Var(b[k]) = k Ts sigma_rw^2, over many independent channels.
        ImperfectionConfig c;
        c.randomWalkSigma = 2.0;
        const double ts = 0.01;
        const int channels = 4000;
        std::vector<double> at25;
        std::vector<double> at100;
        for (int i = 0; i < channels; ++i) {
            ChannelImperfections channel(c, ts, std::uint64_t(1000 + i));
            for (int k = 1; k <= 100; ++k) {
                const double y = channel.apply(0.0);
                if (k == 25) {
                    at25.push_back(y);
                }
                if (k == 100) {
                    at100.push_back(y);
                }
            }
            QCOMPARE(channel.randomWalkBias(), at100.back());
        }
        const double v25 = stats(at25).variance;
        const double v100 = stats(at100).variance;
        QVERIFY2(std::fabs(v25 / (25 * ts * 4.0) - 1.0) < 0.1, qPrintable(QString::number(v25)));
        QVERIFY2(std::fabs(v100 / (100 * ts * 4.0) - 1.0) < 0.1, qPrintable(QString::number(v100)));
        QVERIFY(std::fabs(v100 / v25 - 4.0) < 0.6);
    }

    void gaussMarkovStationaryVariance() {
        ImperfectionConfig c;
        c.gaussMarkovSigma = 0.3;
        c.gaussMarkovTau = 0.05;
        const std::vector<double> out = run(c, std::vector<double>(400000, 0.0), 0.01);
        const Stats s = stats(out);
        QVERIFY2(std::fabs(s.variance / 0.09 - 1.0) < 0.1, qPrintable(QString::number(s.variance)));
        QVERIFY(std::fabs(s.mean) < 0.02);
        // Lag-1 autocorrelation e^(-Ts/tau).
        double num = 0.0;
        for (std::size_t i = 1; i < out.size(); ++i) {
            num += out[i] * out[i - 1];
        }
        const double rho = num / double(out.size() - 1) / s.variance;
        QVERIFY2(std::fabs(rho - std::exp(-0.2)) < 0.02, qPrintable(QString::number(rho)));
    }

    void scaleAndBias() {
        ImperfectionConfig c;
        c.scale = 0.1;
        c.bias = -0.5;
        QCOMPARE(run(c, {2.0}).front(), 1.1 * 2.0 - 0.5);
    }

    void saturationAndQuantization() {
        ImperfectionConfig c;
        c.saturationMin = -1.0;
        c.saturationMax = 2.0;
        c.quantization = 0.25;
        const std::vector<double> out = run(c, {-5.0, 0.3, 0.38, 1.9, 7.0});
        QCOMPARE(out[0], -1.0);
        QCOMPARE(out[1], 0.25);
        QCOMPARE(out[2], 0.5);
        QCOMPARE(out[3], 2.0);
        QCOMPARE(out[4], 2.0);
        // NaN passes through.
        QVERIFY(std::isnan(run(c, {std::nan("")}).front()));
    }

    void delay() {
        ImperfectionConfig c;
        c.delaySamples = 2;
        const std::vector<double> out = run(c, {1, 2, 3, 4, 5});
        QCOMPARE(out, (std::vector<double>{1, 1, 1, 2, 3}));
    }

    void ownSampleRateHolds() {
        ImperfectionConfig c;
        c.rateDivider = 3;
        const std::vector<double> out = run(c, {1, 2, 3, 4, 5, 6, 7});
        QCOMPARE(out, (std::vector<double>{1, 1, 1, 4, 4, 4, 7}));
    }

    void dropout() {
        ImperfectionConfig c;
        c.dropoutProbability = 0.3;
        std::vector<double> input;
        for (int i = 0; i < 20000; ++i) {
            input.push_back(i);
        }
        ChannelImperfections hold(c, 0.01, 11);
        c.dropoutMode = DropoutMode::NaN;
        ChannelImperfections nan(c, 0.01, 11);
        int dropped = 0;
        double last = std::nan("");
        for (double v : input) {
            const double h = hold.apply(v);
            const double n = nan.apply(v);
            if (hold.lastDropped()) {
                ++dropped;
                QVERIFY(std::isnan(n));
                QVERIFY(std::isnan(last) ? std::isnan(h) : h == last);
            } else {
                QCOMPARE(h, v);
                QCOMPARE(n, v);
            }
            QCOMPARE(nan.lastDropped(), hold.lastDropped());  // same stream
            last = h;
        }
        QVERIFY2(std::fabs(dropped / 20000.0 - 0.3) < 0.015, qPrintable(QString::number(dropped)));
    }

    void outliers() {
        ImperfectionConfig c;
        c.outlierProbability = 0.05;
        c.outlierMagnitude = 10.0;
        const std::vector<double> out = run(c, std::vector<double>(20000, 1.0));
        int up = 0;
        int down = 0;
        for (double v : out) {
            if (v == 11.0) {
                ++up;
            } else if (v == -9.0) {
                ++down;
            } else {
                QCOMPARE(v, 1.0);
            }
        }
        QVERIFY(std::fabs((up + down) / 20000.0 - 0.05) < 0.006);
        QVERIFY(up > 300 && down > 300);
    }

    void switchingAModelKeepsTheOthersNoise() {
        ImperfectionConfig white;
        white.whiteSigma = 1.0;
        ImperfectionConfig both = white;
        both.outlierProbability = 0.2;
        both.outlierMagnitude = 100.0;
        const std::vector<double> a = run(white, std::vector<double>(200, 0.0));
        const std::vector<double> b = run(both, std::vector<double>(200, 0.0));
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = b[i] - a[i];
            QVERIFY(d == 0.0 || std::fabs(std::fabs(d) - 100.0) < 1e-9);
        }
    }

    void invalidConfigsThrow() {
        ImperfectionConfig c;
        c.dropoutProbability = 1.5;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ChannelImperfections(c, 0.01, 1));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ChannelImperfections({}, 0.0, 1));
        ImperfectionConfig r;
        r.rateDivider = 0;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ChannelImperfections(r, 0.01, 1));
    }

    void linearObservation() {
        Eigen::MatrixXd h(2, 3);
        h << 1, 2, 0, 0, -1, 3;
        Eigen::MatrixXd d(2, 1);
        d << 0.5, 0;
        LinearObservation obs("lin", h, d);
        QVERIFY_THROWS_EXCEPTION(std::logic_error,
                                 obs.sample(Eigen::Vector3d(1, 2, 3), Eigen::VectorXd::Ones(1)));
        obs.configure(0.01, 1);
        obs.sample(Eigen::Vector3d(1, 2, 3), Eigen::VectorXd::Constant(1, 4.0));
        QCOMPARE(obs.trueOutput()[0], 1 + 4 + 2.0);
        QCOMPARE(obs.trueOutput()[1], -2 + 9.0);
        QCOMPARE(obs.measured()[0], obs.trueOutput()[0]);  // no imperfections
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 obs.sample(Eigen::Vector2d(1, 2), Eigen::VectorXd::Ones(1)));
        // No D: u is ignored.
        LinearObservation noD("noD", h);
        noD.configure(0.01, 1);
        noD.sample(Eigen::Vector3d(1, 2, 3), {});
        QCOMPARE(noD.trueOutput()[1], 7.0);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 LinearObservation("bad", h, Eigen::MatrixXd::Ones(3, 1)));
    }

    void nonlinearRangeBearingWithNoise() {
        ImperfectionConfig noise;
        noise.whiteSigma = 0.05;
        SensorSet set;
        auto& nl = set.emplace<NonlinearObservation>("range_bearing", 2,
                                                     NonlinearObservation::Function(rangeBearing));
        nl.setAllImperfections(noise);
        int calls = 0;
        auto& raw = set.emplace<NonlinearObservation>("range_bearing_raw", 2, &rangeBearingRaw,
                                                      static_cast<void*>(&calls));
        auto& perChannel = set.emplace<NonlinearObservation>(
            "range_bearing_channels",
            std::vector<NonlinearObservation::ChannelFunction>{
                [](const Eigen::VectorXd& x, const Eigen::VectorXd&, const SensorEnvironment&) {
                    return std::hypot(x[0], x[1]);
                },
                [](const Eigen::VectorXd& x, const Eigen::VectorXd&, const SensorEnvironment&) {
                    return std::atan2(x[1], x[0]);
                }});
        // A linear block in the same set: the position itself.
        auto& lin = set.emplace<LinearObservation>("position", Eigen::MatrixXd::Identity(2, 2));
        QCOMPARE(set.size(), 4);
        QVERIFY_THROWS_EXCEPTION(
            std::invalid_argument,
            set.emplace<LinearObservation>("position", Eigen::MatrixXd::Identity(2, 2)));
        set.configure(0.01, 2024);

        const Eigen::Vector2d x(3.0, 4.0);
        std::vector<double> rangeErr;
        std::vector<double> bearingErr;
        for (int k = 0; k < 50000; ++k) {
            set.sample(x, {});
            rangeErr.push_back(nl.measured()[0] - 5.0);
            bearingErr.push_back(nl.measured()[1] - std::atan2(4.0, 3.0));
        }
        QCOMPARE(nl.trueOutput()[0], 5.0);
        QCOMPARE(nl.trueOutput()[1], std::atan2(4.0, 3.0));
        QCOMPARE(raw.measured()[0], 5.0);
        QCOMPARE(raw.measured()[1], std::atan2(4.0, 3.0));
        QCOMPARE(calls, 50000);
        QCOMPARE(perChannel.trueOutput()[0], 5.0);
        QCOMPARE(perChannel.measured()[1], std::atan2(4.0, 3.0));
        QCOMPARE(lin.measured()[1], 4.0);
        for (const std::vector<double>* err : {&rangeErr, &bearingErr}) {
            const Stats s = stats(*err);
            QVERIFY(std::fabs(s.mean) < 0.002);
            QVERIFY(std::fabs(std::sqrt(s.variance) - 0.05) < 0.002);
        }
        QVERIFY(set.find("range_bearing") == &nl);
        QVERIFY(set.find("missing") == nullptr);

        // A function returning the wrong size.
        NonlinearObservation wrong("wrong", 3, NonlinearObservation::Function(rangeBearing));
        wrong.configure(0.01, 1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, wrong.sample(x, {}));
    }

    void environmentReachesTheFunction() {
        const double offset = 10.0;
        NonlinearObservation obs(
            "env", 1,
            NonlinearObservation::Function(
                [](const Eigen::VectorXd& x, const Eigen::VectorXd& u, const SensorEnvironment& e) {
                    Eigen::VectorXd y(1);
                    y << x[0] + u[0] + *static_cast<const double*>(e.user) + e.t;
                    return y;
                }));
        obs.configure(0.1, 1);
        SensorEnvironment env;
        env.user = &offset;
        env.t = 0.5;
        obs.sample(Eigen::VectorXd::Constant(1, 1.0), Eigen::VectorXd::Constant(1, 2.0), env);
        QCOMPARE(obs.measured()[0], 13.5);
    }
};

QTEST_GUILESS_MAIN(TestSensors)
#include "test_sensors.moc"
