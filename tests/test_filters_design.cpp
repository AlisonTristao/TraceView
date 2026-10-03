// The host-side IIR design (lab/filters/design/tlfilterdesign.h) and the C
// biquad cascade that runs it (lab/filters/runtime/tlfilter.h, here built in
// double, tracelab_filters_rt_f64): the magnitude responses against the
// closed-form digital (bilinear) Butterworth / Chebyshev I / Chebyshev II
// formulas (Butterworth -3 dB at fc, the Chebyshev ripple and stopband
// edges), band-pass/band-stop shapes, the C runtime reproducing Iir1's own
// filtering sample by sample for every family and response, a notch removing
// 50 Hz, and the invalid specs.

#include <QtTest>
#include <Iir.h>
#include <cmath>
#include <functional>
#include <random>
#include <type_traits>
#include <vector>

#include "lab/filters/design/tlfilterdesign.h"
#include "lab/filters/runtime/tlfilter.h"

using namespace tlfd;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 1000.0;

double warp(double f) { return std::tan(kPi * f / kFs); }

// Chebyshev polynomial T_n(x), |x| <= 1 and beyond.
double cheb(int n, double x) {
    if (std::abs(x) <= 1) return std::cos(n * std::acos(x));
    return std::cosh(n * std::acosh(std::abs(x))) * ((x < 0 && (n % 2)) ? -1 : 1);
}

double db(double mag2) { return 10 * std::log10(mag2); }

// A test signal: a chirp through the whole band plus noise and a DC step.
std::vector<double> testSignal(int n) {
    std::vector<double> x(n);
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.3);
    for (int i = 0; i < n; ++i) {
        const double t = i / kFs;
        x[i] = std::sin(2 * kPi * (5 + 200 * t) * t) + noise(rng) + (i > n / 2 ? 0.7 : 0.0);
    }
    return x;
}

// Runs `design` in the C runtime and `iir` (an Iir1 filter set up through
// its own user-facing API) on the same signal; returns the largest
// difference.
double runtimeVersusIir1(const Design& d, const std::function<double(double)>& iir) {
    tlf_biquad bq;
    if (toRuntime(d, &bq) != TLF_OK) return 1e9;
    double worst = 0;
    for (double x : testSignal(4000))
        worst = std::max(worst, std::abs(tlf_biquad_step(&bq, x) - iir(x)));
    return worst;
}

Spec spec(Family fam, Response r, int order, double f, double w = 0) {
    Spec s;
    s.family = fam;
    s.response = r;
    s.order = order;
    s.sampleRate = kFs;
    s.frequency = f;
    s.width = w;
    s.rippleDb = 1.0;
    s.stopbandDb = 40.0;
    return s;
}

}  // namespace

class TestFiltersDesign : public QObject {
    Q_OBJECT

private slots:
    void runtimeIsDouble();
    void butterworthLowPassMagnitude();
    void butterworthHighPassMagnitude();
    void chebyshevILowPassMagnitude();
    void chebyshevIILowPassMagnitude();
    void bandPassAndBandStop();
    void runtimeMatchesIir1_data();
    void runtimeMatchesIir1();
    void notchRemoves50Hz();
    void invalidSpecs();
};

void TestFiltersDesign::runtimeIsDouble() {
    QVERIFY((std::is_same<tlf_real, double>::value));
}

void TestFiltersDesign::butterworthLowPassMagnitude() {
    for (int order : {1, 2, 4, 7}) {
        const Design d = design(spec(Family::Butterworth, Response::LowPass, order, 100));
        QVERIFY2(d.ok, d.error.c_str());
        QCOMPARE(int(d.sections.size()), (order + 1) / 2);
        QVERIFY(std::abs(magnitudeDb(d, kFs, 0.0)) < 1e-9);
        QVERIFY(std::abs(magnitudeDb(d, kFs, 100.0) - db(0.5)) < 1e-9);  // -3.0103 dB
        // |H|^2 = 1 / (1 + (tan(pi f/fs) / tan(pi fc/fs))^2N) everywhere.
        for (double f : {10.0, 50.0, 150.0, 300.0, 450.0}) {
            const double theory = db(1 / (1 + std::pow(warp(f) / warp(100), 2 * order)));
            QVERIFY2(std::abs(magnitudeDb(d, kFs, f) - theory) < 1e-7,
                     qPrintable(QString("order %1 f %2").arg(order).arg(f)));
        }
    }
}

void TestFiltersDesign::butterworthHighPassMagnitude() {
    const int order = 4;
    const Design d = design(spec(Family::Butterworth, Response::HighPass, order, 50));
    QVERIFY2(d.ok, d.error.c_str());
    QVERIFY(std::abs(magnitudeDb(d, kFs, 499.999)) < 1e-6);
    QVERIFY(std::abs(magnitudeDb(d, kFs, 50.0) - db(0.5)) < 1e-9);
    for (double f : {5.0, 20.0, 100.0, 300.0}) {
        const double theory = db(1 / (1 + std::pow(warp(50) / warp(f), 2 * order)));
        QVERIFY(std::abs(magnitudeDb(d, kFs, f) - theory) < 1e-7);
    }
    QVERIFY(magnitudeDb(d, kFs, 5.0) < -75);
}

void TestFiltersDesign::chebyshevILowPassMagnitude() {
    for (int order : {3, 4}) {
        const Design d = design(spec(Family::ChebyshevI, Response::LowPass, order, 100));
        QVERIFY2(d.ok, d.error.c_str());
        // |H|^2 = 1 / (1 + eps^2 T_N^2(W / Wc)), eps^2 = 10^(ripple/10) - 1:
        // equiripple between 0 and -1 dB up to fc, -1 dB at fc.
        const double eps2 = std::pow(10.0, 1.0 / 10) - 1;
        double maxPass = -1e9, minPass = 1e9;
        for (double f = 0; f <= 100.0; f += 0.5) {
            const double m = magnitudeDb(d, kFs, f);
            const double t = cheb(order, warp(f) / warp(100));
            const double theory = db(1 / (1 + eps2 * t * t));
            QVERIFY2(std::abs(m - theory) < 1e-7,
                     qPrintable(QString("order %1 f %2: %3 vs %4")
                                    .arg(order).arg(f).arg(m).arg(theory)));
            maxPass = std::max(maxPass, m);
            minPass = std::min(minPass, m);
        }
        QVERIFY(maxPass < 1e-9 && maxPass > -0.01);
        QVERIFY(minPass > -1.0 - 1e-9 && minPass < -0.99);
        QVERIFY(std::abs(magnitudeDb(d, kFs, 100.0) + 1.0) < 1e-9);
        QVERIFY(magnitudeDb(d, kFs, 300.0) < -40);
    }
}

void TestFiltersDesign::chebyshevIILowPassMagnitude() {
    const int order = 5;
    const Design d = design(spec(Family::ChebyshevII, Response::LowPass, order, 100));
    QVERIFY2(d.ok, d.error.c_str());
    // |H|^2 = 1 / (1 + 1 / (eps^2 T_N^2(Wc / W))), eps^2 = 1 / (10^(A/10) - 1):
    // flat passband, at most -40 dB from fc on, exactly -40 dB at fc.
    const double eps2 = 1 / (std::pow(10.0, 40.0 / 10) - 1);
    QVERIFY(std::abs(magnitudeDb(d, kFs, 0.0)) < 1e-9);
    QVERIFY(std::abs(magnitudeDb(d, kFs, 100.0) + 40.0) < 1e-6);
    for (double f = 1; f < 499; f += 7) {
        const double t = cheb(order, warp(100) / warp(f));
        const double theory = db(1 / (1 + 1 / (eps2 * t * t)));
        QVERIFY2(std::abs(magnitudeDb(d, kFs, f) - theory) < 1e-6, qPrintable(QString::number(f)));
        if (f >= 100) QVERIFY(magnitudeDb(d, kFs, f) < -40 + 1e-6);
    }
}

void TestFiltersDesign::bandPassAndBandStop() {
    const Design bp = design(spec(Family::Butterworth, Response::BandPass, 3, 100, 20));
    QVERIFY2(bp.ok, bp.error.c_str());
    QCOMPARE(int(bp.sections.size()), 3);  // 2N poles
    QVERIFY(std::abs(magnitudeDb(bp, kFs, 100.0)) < 1e-6);
    QVERIFY(magnitudeDb(bp, kFs, 10.0) < -60);
    QVERIFY(magnitudeDb(bp, kFs, 400.0) < -60);
    QVERIFY(std::abs(magnitudeDb(bp, kFs, 90.0) - db(0.5)) < 1e-6);
    QVERIFY(std::abs(magnitudeDb(bp, kFs, 110.0) - db(0.5)) < 1e-6);

    const Design bs = design(spec(Family::Butterworth, Response::BandStop, 2, 100, 20));
    QVERIFY2(bs.ok, bs.error.c_str());
    // Iir1 puts the band edges, center -+ width / 2, at -3 dB; the notch of
    // the band-stop sits near (not exactly at) the center.
    QVERIFY(std::abs(magnitudeDb(bs, kFs, 90.0) - db(0.5)) < 1e-6);
    QVERIFY(std::abs(magnitudeDb(bs, kFs, 110.0) - db(0.5)) < 1e-6);
    double deepest = 0;
    for (double f = 95; f < 105; f += 0.01) deepest = std::min(deepest, magnitudeDb(bs, kFs, f));
    QVERIFY(deepest < -80);
    QVERIFY(magnitudeDb(bs, kFs, 100.0) < -40);
    QVERIFY(std::abs(magnitudeDb(bs, kFs, 0.0)) < 1e-6);
    QVERIFY(std::abs(magnitudeDb(bs, kFs, 499.999)) < 1e-6);
    QVERIFY(magnitudeDb(bs, kFs, 200.0) > -0.5);

    // The Chebyshev families design every response too.
    for (Family fam : {Family::ChebyshevI, Family::ChebyshevII})
        for (Response r : {Response::HighPass, Response::BandPass, Response::BandStop}) {
            const Design d = design(spec(fam, r, 4, 120, 40));
            QVERIFY2(d.ok, d.error.c_str());
        }
}

void TestFiltersDesign::runtimeMatchesIir1_data() {
    QTest::addColumn<int>("which");
    const char* names[] = {"butterworth lp",  "butterworth hp",  "butterworth bp",
                           "butterworth bs",  "chebyshev1 lp",   "chebyshev1 hp",
                           "chebyshev1 bp",   "chebyshev1 bs",   "chebyshev2 lp",
                           "chebyshev2 hp",   "chebyshev2 bp",   "chebyshev2 bs"};
    for (int i = 0; i < 12; ++i) QTest::newRow(names[i]) << i;
}

void TestFiltersDesign::runtimeMatchesIir1() {
    QFETCH(int, which);
    const Response responses[] = {Response::LowPass, Response::HighPass, Response::BandPass,
                                  Response::BandStop};
    const Family fam = which < 4 ? Family::Butterworth
                                 : (which < 8 ? Family::ChebyshevI : Family::ChebyshevII);
    const Response r = responses[which % 4];
    const bool band = r == Response::BandPass || r == Response::BandStop;
    constexpr int kOrder = 4;
    const double f = band ? 150.0 : 80.0, w = 60.0;
    const Design d = design(spec(fam, r, kOrder, f, band ? w : 0.0));
    QVERIFY2(d.ok, d.error.c_str());

    // The reference: Iir1 set up through its own user-facing API (not the
    // base-class path tlfilterdesign.cpp uses) and running its own
    // Direct Form II, sample by sample.
    double worst = 1e9;
    switch (which) {
        case 0: { Iir::Butterworth::LowPass<kOrder> x; x.setup(kFs, f);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 1: { Iir::Butterworth::HighPass<kOrder> x; x.setup(kFs, f);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 2: { Iir::Butterworth::BandPass<kOrder> x; x.setup(kFs, f, w);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 3: { Iir::Butterworth::BandStop<kOrder> x; x.setup(kFs, f, w);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 4: { Iir::ChebyshevI::LowPass<kOrder> x; x.setup(kFs, f, 1.0);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 5: { Iir::ChebyshevI::HighPass<kOrder> x; x.setup(kFs, f, 1.0);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 6: { Iir::ChebyshevI::BandPass<kOrder> x; x.setup(kFs, f, w, 1.0);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 7: { Iir::ChebyshevI::BandStop<kOrder> x; x.setup(kFs, f, w, 1.0);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 8: { Iir::ChebyshevII::LowPass<kOrder> x; x.setup(kFs, f, 40.0);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 9: { Iir::ChebyshevII::HighPass<kOrder> x; x.setup(kFs, f, 40.0);
                  worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 10: { Iir::ChebyshevII::BandPass<kOrder> x; x.setup(kFs, f, w, 40.0);
                   worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
        case 11: { Iir::ChebyshevII::BandStop<kOrder> x; x.setup(kFs, f, w, 40.0);
                   worst = runtimeVersusIir1(d, [&](double v) { return x.filter(v); }); break; }
    }
    // Direct Form II (Iir1) and II transposed (runtime) differ only in
    // round-off.
    QVERIFY2(worst < 1e-11, qPrintable(QString::number(worst, 'g', 3)));
}

void TestFiltersDesign::notchRemoves50Hz() {
    const Design d = notch(kFs, 50.0, 10.0);
    QVERIFY2(d.ok, d.error.c_str());
    QCOMPARE(int(d.sections.size()), 1);
    QVERIFY(magnitudeDb(d, kFs, 50.0) < -100);
    // Iir1's IIRNotch is not unity outside the notch: about +0.1 dB.
    QVERIFY(std::abs(magnitudeDb(d, kFs, 5.0)) < 0.2);
    QVERIFY(std::abs(magnitudeDb(d, kFs, 200.0)) < 0.2);
    QVERIFY(magnitudeDb(d, kFs, 45.0) < -0.5);

    // A 5 Hz signal with 50 Hz mains hum on top: after the transient, what
    // the hum adds to the output (the output minus the same filter's output
    // for the clean signal alone) is under 1 % of the hum.
    tlf_biquad bq, bqClean;
    QCOMPARE(toRuntime(d, &bq), int(TLF_OK));
    QCOMPARE(toRuntime(d, &bqClean), int(TLF_OK));
    Iir::RBJ::IIRNotch ref;
    ref.setup(kFs, 50.0, 10.0);
    double worstResidual = 0, worstVsIir1 = 0;
    for (int i = 0; i < 3000; ++i) {
        const double t = i / kFs;
        const double clean = 0.5 * std::sin(2 * kPi * 5 * t);
        const double x = clean + 1.0 * std::sin(2 * kPi * 50 * t);
        const double y = tlf_biquad_step(&bq, x);
        worstVsIir1 = std::max(worstVsIir1, std::abs(y - ref.filter(x)));
        const double yClean = tlf_biquad_step(&bqClean, clean);
        if (i > 1000) worstResidual = std::max(worstResidual, std::abs(y - yClean));
    }
    QVERIFY2(worstResidual < 0.01, qPrintable(QString::number(worstResidual)));
    QVERIFY(worstVsIir1 < 1e-12);
}

void TestFiltersDesign::invalidSpecs() {
    QVERIFY(!design(spec(Family::Butterworth, Response::LowPass, 0, 100)).ok);
    QVERIFY(!design(spec(Family::Butterworth, Response::LowPass, kMaxOrder + 1, 100)).ok);
    QVERIFY(!design(spec(Family::Butterworth, Response::LowPass, 2, 500)).ok);  // Nyquist
    QVERIFY(!design(spec(Family::Butterworth, Response::BandPass, 2, 100, 0)).ok);
    QVERIFY(!design(spec(Family::Butterworth, Response::BandPass, 2, 480, 60)).ok);
    QVERIFY(!design(spec(Family::Butterworth, Response::BandStop, 2, 20, 60)).ok);
    const Design bad = design(spec(Family::Butterworth, Response::LowPass, 2, -1));
    QVERIFY(!bad.ok);
    QVERIFY(!bad.error.empty());
    QVERIFY(!notch(kFs, 600.0).ok);
    // The design can be longer than the runtime's fixed cascade.
    const Design longBand = design(spec(Family::Butterworth, Response::BandPass, 12, 100, 20));
    QVERIFY(longBand.ok);
    tlf_biquad bq;
    QCOMPARE(toRuntime(longBand, &bq), int(TLF_EINVAL));
    QCOMPARE(toRuntime(bad, &bq), int(TLF_EINVAL));
}

QTEST_GUILESS_MAIN(TestFiltersDesign)
#include "test_filters_design.moc"
