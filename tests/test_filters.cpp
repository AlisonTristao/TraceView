// The C filter runtime (lab/filters/runtime/tlfilter.h), built the way the
// firmware builds it (tlf_real = float, tracelab_filters_rt): moving average
// and median on known sequences, the first-order low-pass time constant and
// high-pass DC rejection, a biquad section against its difference equation,
// the complementary filter fusing a biased gyro with a noisy angle, the
// filtered derivative on a ramp, rate limiter and dead zone, the chain and
// bank plumbing, the user block called with its state, and the no-allocation
// contract (every state is a plain struct the caller owns; the library never
// references malloc). The IIR designs are in test_filters_design.cpp, the
// Kalman filters in test_filters_kalman.cpp.

#include <QtTest>
#include <cmath>
#include <random>
#include <type_traits>
#include <vector>

#include "lab/filters/runtime/tlfilter.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

class TestFilters : public QObject {
    Q_OBJECT

private slots:
    void floatByDefault();
    void movingAverageKnownSequence();
    void movingAverageNoDrift();
    void medianKnownSequence();
    void medianRejectsSpikes();
    void lowPassTimeConstant();
    void lowPassPrime();
    void highPassRemovesDc();
    void biquadSectionMatchesDifferenceEquation();
    void complementaryFusesBiasedGyro();
    void derivativeOfRamp();
    void rateLimiter();
    void deadZone();
    void chainAndBank();
    void userBlockGetsItsState();
    void invalidParameters();
    void noAllocationByConstruction();
};

void TestFilters::floatByDefault() {
    QVERIFY((std::is_same<tlf_real, float>::value));
}

void TestFilters::movingAverageKnownSequence() {
    tlf_real buf[3];
    tlf_movavg ma;
    QCOMPARE(tlf_movavg_init(&ma, buf, 3), int(TLF_OK));
    // Warm-up averages what has arrived, then the last three.
    const float in[] = {3, 6, 9, 12, 0, 3};
    const float expected[] = {3, 4.5f, 6, 9, 7, 5};
    for (int i = 0; i < 6; ++i) QCOMPARE(tlf_movavg_step(&ma, in[i]), expected[i]);
    tlf_movavg_reset(&ma);
    QCOMPARE(tlf_movavg_step(&ma, 10.0f), 10.0f);
}

void TestFilters::movingAverageNoDrift() {
    // A million samples of a large offset plus small noise: the running sum
    // is re-summed once per lap, so the float average stays exact to the
    // window's own rounding.
    std::vector<tlf_real> buf(16);
    tlf_movavg ma;
    QCOMPARE(tlf_movavg_init(&ma, buf.data(), 16), int(TLF_OK));
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (int i = 0; i < 1000000; ++i) tlf_movavg_step(&ma, 1000.0f + noise(rng));
    double exact = 0;
    for (int i = 0; i < 16; ++i) tlf_movavg_step(&ma, 0.25f * i), exact += 0.25 * i;
    exact /= 16;
    QVERIFY(std::abs(ma.sum / 16 - exact) < 1e-5);
}

void TestFilters::medianKnownSequence() {
    tlf_median m;
    QCOMPARE(tlf_median_init(&m, 5), int(TLF_OK));
    const float in[] = {5, 1, 4, 2, 3, 100, -50, 7};
    // windows: {5} {5,1} {5,1,4} {5,1,4,2} {5,1,4,2,3} {1,4,2,3,100}
    //          {4,2,3,100,-50} {2,3,100,-50,7}
    const float expected[] = {5, 3, 4, 3, 3, 3, 3, 3};
    for (int i = 0; i < 8; ++i) QCOMPARE(tlf_median_step(&m, in[i]), expected[i]);
    // Duplicates leave and enter the window correctly.
    tlf_median d;
    QCOMPARE(tlf_median_init(&d, 3), int(TLF_OK));
    const float din[] = {2, 2, 2, 9, 9, 1, 1};
    const float dexp[] = {2, 2, 2, 2, 9, 9, 1};
    for (int i = 0; i < 7; ++i) QCOMPARE(tlf_median_step(&d, din[i]), dexp[i]);
    QCOMPARE(tlf_median_init(&d, TLF_MEDIAN_MAX), int(TLF_OK));
    QCOMPARE(tlf_median_init(&d, TLF_MEDIAN_MAX + 1), int(TLF_EINVAL));
}

void TestFilters::medianRejectsSpikes() {
    tlf_median m;
    QCOMPARE(tlf_median_init(&m, 7), int(TLF_OK));
    for (int i = 0; i < 200; ++i) {
        const float clean = std::sin(0.01f * i);
        const float x = (i % 10 == 3) ? clean + 50.0f : clean;  // one spike in ten
        const float y = tlf_median_step(&m, x);
        if (i > 7) QVERIFY(std::abs(y - clean) < 0.05f);
    }
}

void TestFilters::lowPassTimeConstant() {
    const float ts = 0.001f, tau = 0.05f;
    tlf_lowpass1 lp;
    QCOMPARE(tlf_lowpass1_init(&lp, tau, ts, 0), int(TLF_OK));
    // Unit step from 0: after k samples y = 1 - exp(-k ts / tau), so 63.2 %
    // at one time constant and 95 % at three.
    const int k = int(std::lround(tau / ts));
    float y = 0;
    for (int i = 1; i <= 3 * k; ++i) {
        y = tlf_lowpass1_step(&lp, 1.0f);
        QVERIFY(std::abs(y - (1.0 - std::exp(-i * double(ts) / tau))) < 1e-5);
        if (i == k) QVERIFY(std::abs(y - (1.0 - std::exp(-1.0))) < 1e-5);
    }
    QVERIFY(std::abs(y - 0.9502) < 1e-3);
}

void TestFilters::lowPassPrime() {
    tlf_lowpass1 lp;
    QCOMPARE(tlf_lowpass1_init(&lp, 1.0f, 0.01f, 1), int(TLF_OK));
    QCOMPARE(tlf_lowpass1_step(&lp, 7.0f), 7.0f);  // no ramp from 0
    QCOMPARE(tlf_lowpass1_step(&lp, 7.0f), 7.0f);
    tlf_lowpass1_reset(&lp);
    QCOMPARE(tlf_lowpass1_step(&lp, -2.0f), -2.0f);
    // tau = 0 is a pass-through.
    QCOMPARE(tlf_lowpass1_init(&lp, 0.0f, 0.01f, 0), int(TLF_OK));
    QCOMPARE(tlf_lowpass1_step(&lp, 3.5f), 3.5f);
}

void TestFilters::highPassRemovesDc() {
    const float ts = 0.001f, tau = 0.02f;
    tlf_highpass1 hp;
    QCOMPARE(tlf_highpass1_init(&hp, tau, ts, 0), int(TLF_OK));
    // Step from 0 (prime off): jumps to a, then decays as exp(-k ts / tau).
    const double a = std::exp(-double(ts) / tau);
    for (int i = 0; i < 200; ++i) {
        const float y = tlf_highpass1_step(&hp, 1.0f);
        QVERIFY(std::abs(y - std::pow(a, i + 1)) < 1e-5);
    }
    // Primed: a DC level never comes through; a 50 Hz sine passes almost whole.
    QCOMPARE(tlf_highpass1_init(&hp, tau, ts, 1), int(TLF_OK));
    float peak = 0;
    for (int i = 0; i < 2000; ++i) {
        const float y = tlf_highpass1_step(&hp, 5.0f + std::sin(2 * float(kPi) * 50 * i * ts));
        if (i > 1000) peak = std::max(peak, std::abs(y));
    }
    QVERIFY(peak > 0.95f && peak < 1.05f);
}

void TestFilters::biquadSectionMatchesDifferenceEquation() {
    // Two sections with arbitrary stable coefficients against the direct
    // difference equations in double.
    const tlf_biquad_coeffs c[2] = {{0.2f, 0.3f, 0.1f, -0.5f, 0.25f},
                                    {1.0f, -1.2f, 0.5f, -0.9f, 0.4f}};
    tlf_biquad bq;
    QCOMPARE(tlf_biquad_init(&bq, c, 2), int(TLF_OK));
    double x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0};
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-1, 1);
    for (int i = 0; i < 500; ++i) {
        const float x = d(rng);
        double v = x;
        for (int s = 0; s < 2; ++s) {
            const double y = double(c[s].b0) * v + double(c[s].b1) * x1[s] +
                             double(c[s].b2) * x2[s] - double(c[s].a1) * y1[s] -
                             double(c[s].a2) * y2[s];
            x2[s] = x1[s], x1[s] = v, y2[s] = y1[s], y1[s] = y;
            v = y;
        }
        QVERIFY(std::abs(tlf_biquad_step(&bq, x) - v) < 1e-5);
    }
    tlf_biquad_reset(&bq);
    QCOMPARE(tlf_biquad_step(&bq, 0.0f), 0.0f);
    QCOMPARE(tlf_biquad_init(&bq, c, 0), int(TLF_EINVAL));
    QCOMPARE(tlf_biquad_init(&bq, c, TLF_BIQUAD_MAX_SECTIONS + 1), int(TLF_EINVAL));
}

void TestFilters::complementaryFusesBiasedGyro() {
    // True angle: a slow sine. Gyro: its derivative + 0.05 rad/s bias + small
    // noise. Absolute source: the angle + large white noise (sigma 0.1 rad).
    const float ts = 0.005f, tau = 0.5f, bias = 0.05f;
    std::mt19937 rng(11);
    std::normal_distribution<float> gyroNoise(0.0f, 0.01f), absNoise(0.0f, 0.1f);

    tlf_complementary classic, withBias;
    QCOMPARE(tlf_complementary_init(&classic, tau, ts, 0.0f), int(TLF_OK));
    QCOMPARE(tlf_complementary_init(&withBias, tau, ts, 0.002f), int(TLF_OK));
    tlf_block blk = tlf_complementary_block(&withBias);
    QCOMPARE(int(blk.n_in), 2);
    QCOMPARE(int(blk.n_out), 1);

    double errClassic = 0, errBias = 0, errRaw = 0, meanClassic = 0, meanBias = 0;
    int n = 0;
    float gyroOnly = 0;
    for (int i = 0; i < 40000; ++i) {  // 200 s
        const float t = i * ts;
        const float angle = 0.3f * std::sin(0.2f * t);
        const float rate = 0.06f * std::cos(0.2f * t);
        const float gyro = rate + bias + gyroNoise(rng);
        const float meas = angle + absNoise(rng);
        const float a = tlf_complementary_step(&classic, gyro, meas);
        float in[2] = {gyro, meas}, out[1];
        blk.step(blk.state, in, out);
        gyroOnly += gyro * ts;
        if (i > 20000) {  // after the bias estimate has settled
            errClassic += double(a - angle) * (a - angle);
            meanClassic += a - angle;
            errBias += double(out[0] - angle) * (out[0] - angle);
            errRaw += double(meas - angle) * (meas - angle);
            meanBias += withBias.bias;
            ++n;
        }
    }
    errClassic = std::sqrt(errClassic / n);
    errBias = std::sqrt(errBias / n);
    errRaw = std::sqrt(errRaw / n);
    meanClassic /= n;
    meanBias /= n;
    // Integrating the gyro alone drifts by bias * t (10 rad after 200 s).
    QVERIFY(std::abs(gyroOnly - 0.3f * std::sin(0.2f * 200.0f)) > 5.0f);
    // Classic: noise well below the absolute source's, offset ~ bias * tau.
    QVERIFY(errClassic < 0.5 * errRaw);
    QVERIFY(std::abs(meanClassic - bias * tau) < 0.01);
    // Bias-estimating: the bias is found and the offset goes away.
    QVERIFY2(std::abs(meanBias - bias) < 0.005, qPrintable(QString::number(meanBias)));
    QVERIFY(std::abs(withBias.bias - bias) < 0.02f);
    QVERIFY(errBias < errClassic);
    QVERIFY(errBias < 0.3 * errRaw);
}

void TestFilters::derivativeOfRamp() {
    const float ts = 0.01f;
    tlf_derivative d;
    QCOMPARE(tlf_derivative_init(&d, 0.05f, ts), int(TLF_OK));
    QCOMPARE(tlf_derivative_step(&d, 4.0f), 0.0f);  // first sample: no kick
    float y = 0;
    for (int i = 1; i < 300; ++i) y = tlf_derivative_step(&d, 4.0f + 2.5f * i * ts);
    QVERIFY(std::abs(y - 2.5f) < 1e-3f);
    // tau = 0: plain backward difference.
    QCOMPARE(tlf_derivative_init(&d, 0.0f, ts), int(TLF_OK));
    tlf_derivative_step(&d, 1.0f);
    QVERIFY(std::abs(tlf_derivative_step(&d, 1.03f) - 3.0f) < 1e-4f);
}

void TestFilters::rateLimiter() {
    tlf_ratelimit r;
    QCOMPARE(tlf_ratelimit_init(&r, 10.0f, 20.0f, 0.01f, 0), int(TLF_OK));
    // Up by 0.1 per sample, down by 0.2.
    for (int i = 1; i <= 10; ++i)
        QVERIFY(std::abs(tlf_ratelimit_step(&r, 5.0f) - 0.1f * i) < 1e-5f);
    for (int i = 0; i < 100; ++i) tlf_ratelimit_step(&r, 5.0f);
    QCOMPARE(tlf_ratelimit_step(&r, 5.0f), 5.0f);  // reached, then holds exactly
    QVERIFY(std::abs(tlf_ratelimit_step(&r, 0.0f) - 4.8f) < 1e-5f);
    QVERIFY(std::abs(tlf_ratelimit_step(&r, 4.75f) - 4.75f) < 1e-6f);  // small moves pass
    QCOMPARE(tlf_ratelimit_init(&r, 1.0f, 1.0f, 0.01f, 1), int(TLF_OK));
    QCOMPARE(tlf_ratelimit_step(&r, 3.0f), 3.0f);
}

void TestFilters::deadZone() {
    tlf_deadzone dz;
    QCOMPARE(tlf_deadzone_init(&dz, 0.5f, 1), int(TLF_OK));
    QCOMPARE(tlf_deadzone_step(&dz, 0.3f), 0.0f);
    QCOMPARE(tlf_deadzone_step(&dz, -0.5f), 0.0f);
    QCOMPARE(tlf_deadzone_step(&dz, 2.0f), 1.5f);
    QCOMPARE(tlf_deadzone_step(&dz, -2.0f), -1.5f);
    QCOMPARE(tlf_deadzone_init(&dz, 0.5f, 0), int(TLF_OK));
    QCOMPARE(tlf_deadzone_step(&dz, 0.3f), 0.0f);
    QCOMPARE(tlf_deadzone_step(&dz, 2.0f), 2.0f);
    QCOMPARE(tlf_deadzone_step(&dz, -0.6f), -0.6f);
}

void TestFilters::chainAndBank() {
    // median(3) -> moving average(2) -> dead zone(1, shifted).
    tlf_median med;
    tlf_movavg ma;
    tlf_deadzone dz;
    tlf_real buf[2];
    QCOMPARE(tlf_median_init(&med, 3), int(TLF_OK));
    QCOMPARE(tlf_movavg_init(&ma, buf, 2), int(TLF_OK));
    QCOMPARE(tlf_deadzone_init(&dz, 1.0f, 1), int(TLF_OK));
    tlf_block blocks[3] = {tlf_median_block(&med), tlf_movavg_block(&ma), tlf_deadzone_block(&dz)};
    tlf_chain chain;
    QCOMPARE(tlf_chain_init(&chain, blocks, 3), int(TLF_OK));
    const float in[] = {4, 100, 4, 6, 8};
    // median: 4, 52, 4, 6, 6 ; average: 4, 28, 28, 5, 6 ; dead zone: 3, 27, 27, 4, 5
    const float expected[] = {3, 27, 27, 4, 5};
    for (int i = 0; i < 5; ++i) {
        float x = in[i];
        tlf_chain_step(&chain, &x, &x);  // out may alias in
        QCOMPARE(x, expected[i]);
    }
    tlf_chain_reset(&chain);
    float x = 2.0f, y = 0;
    tlf_chain_step(&chain, &x, &y);
    QCOMPARE(y, 1.0f);

    // A 2-channel bank feeding the complementary filter's two inputs.
    tlf_lowpass1 lp0, lp1;
    QCOMPARE(tlf_lowpass1_init(&lp0, 0.0f, 0.01f, 0), int(TLF_OK));  // pass-through
    QCOMPARE(tlf_lowpass1_init(&lp1, 0.0f, 0.01f, 0), int(TLF_OK));
    tlf_block bankBlocks[2] = {tlf_lowpass1_block(&lp0), tlf_lowpass1_block(&lp1)};
    tlf_bank bank;
    QCOMPARE(tlf_bank_init(&bank, bankBlocks, 2), int(TLF_OK));
    tlf_complementary cf;
    QCOMPARE(tlf_complementary_init(&cf, 0.1f, 0.01f, 0.0f), int(TLF_OK));
    tlf_block vec[2] = {tlf_bank_block(&bank), tlf_complementary_block(&cf)};
    tlf_chain vchain;
    QCOMPARE(tlf_chain_init(&vchain, vec, 2), int(TLF_OK));
    QCOMPARE(int(vchain.n_in), 2);
    QCOMPARE(int(vchain.n_out), 1);
    float v[2] = {0.0f, 0.7f}, out = 0;
    tlf_chain_step(&vchain, v, &out);
    QCOMPARE(out, 0.7f);  // primed by the absolute input

    // Mismatched widths are refused.
    tlf_block bad[2] = {tlf_median_block(&med), tlf_complementary_block(&cf)};
    QCOMPARE(tlf_chain_init(&vchain, bad, 2), int(TLF_EINVAL));
    QCOMPARE(tlf_bank_init(&bank, vec, 2), int(TLF_EINVAL));  // not all 1 -> 1
}

namespace {

// A hand-written filter: gain then offset, counting its calls.
struct Affine {
    float gain;
    float offset;
    int calls;
    int resets;
};
void affineStep(void* state, const tlf_real* in, tlf_real* out) {
    auto* s = static_cast<Affine*>(state);
    ++s->calls;
    out[0] = s->gain * in[0] + s->offset;
    out[1] = s->gain * in[1] + s->offset;
}
void affineReset(void* state) { ++static_cast<Affine*>(state)->resets; }

}  // namespace

void TestFilters::userBlockGetsItsState() {
    Affine a{2.0f, 1.0f, 0, 0};
    tlf_block blocks[1] = {tlf_block_make(affineStep, affineReset, &a, 2, 2)};
    tlf_chain c;
    QCOMPARE(tlf_chain_init(&c, blocks, 1), int(TLF_OK));
    float in[2] = {3.0f, -1.0f}, out[2];
    tlf_chain_step(&c, in, out);
    tlf_chain_step(&c, in, out);
    QCOMPARE(out[0], 7.0f);
    QCOMPARE(out[1], -1.0f);
    QCOMPARE(a.calls, 2);
    tlf_chain_reset(&c);
    QCOMPARE(a.resets, 1);
    // A user block in the middle of a chain of library blocks.
    tlf_lowpass1 lp;
    QCOMPARE(tlf_lowpass1_init(&lp, 0.0f, 0.01f, 0), int(TLF_OK));
    Affine b{-1.0f, 0.5f, 0, 0};
    tlf_block mix[3] = {tlf_lowpass1_block(&lp), tlf_block_make(affineStep, nullptr, &b, 1, 1),
                        tlf_lowpass1_block(&lp)};
    QCOMPARE(tlf_chain_init(&c, mix, 3), int(TLF_OK));
}

void TestFilters::invalidParameters() {
    tlf_movavg ma;
    tlf_real buf[1];
    QCOMPARE(tlf_movavg_init(&ma, buf, 0), int(TLF_EINVAL));
    QCOMPARE(tlf_movavg_init(&ma, nullptr, 4), int(TLF_EINVAL));
    tlf_lowpass1 lp;
    QCOMPARE(tlf_lowpass1_init(&lp, -1.0f, 0.01f, 0), int(TLF_EINVAL));
    QCOMPARE(tlf_lowpass1_init(&lp, 1.0f, 0.0f, 0), int(TLF_EINVAL));
    QCOMPARE(tlf_lowpass1_init(&lp, std::nanf(""), 0.01f, 0), int(TLF_EINVAL));
    tlf_highpass1 hp;
    QCOMPARE(tlf_highpass1_init(&hp, 0.0f, 0.01f, 0), int(TLF_EINVAL));
    tlf_ratelimit r;
    QCOMPARE(tlf_ratelimit_init(&r, 0.0f, 1.0f, 0.01f, 0), int(TLF_EINVAL));
    tlf_deadzone dz;
    QCOMPARE(tlf_deadzone_init(&dz, -1.0f, 0), int(TLF_EINVAL));
    tlf_chain c;
    QCOMPARE(tlf_chain_init(&c, nullptr, 1), int(TLF_EINVAL));
    tlf_block wide = tlf_block_make(affineStep, nullptr, nullptr, TLF_MAX_CHANNELS + 1, 1);
    QCOMPARE(tlf_chain_init(&c, &wide, 1), int(TLF_EINVAL));
}

void TestFilters::noAllocationByConstruction() {
    // Every state is a trivially copyable struct of fixed size the caller
    // places where it wants (here: static storage) -- nothing to free, no
    // hidden heap pointer. The moving average's ring is the caller's buffer.
    QVERIFY(std::is_trivially_copyable<tlf_median>::value);
    QVERIFY(std::is_trivially_copyable<tlf_biquad>::value);
    QVERIFY(std::is_trivially_copyable<tlf_kf>::value);
    QVERIFY(std::is_trivially_copyable<tlf_ekf>::value);
    QVERIFY(std::is_trivially_copyable<tlf_chain>::value);
    static tlf_median s_med;
    static tlf_biquad s_bq;
    QCOMPARE(tlf_median_init(&s_med, 9), int(TLF_OK));
    const tlf_biquad_coeffs one = {1, 0, 0, 0, 0};
    QCOMPARE(tlf_biquad_init(&s_bq, &one, 1), int(TLF_OK));
    QCOMPARE(tlf_biquad_step(&s_bq, 0.5f), 0.5f);
    // The firmware budget of the documented defaults (float).
    QVERIFY(sizeof(tlf_median) <= 256);
    QVERIFY(sizeof(tlf_biquad) <= 240);
    QVERIFY(sizeof(tlf_kf) <= 720);
    QVERIFY(sizeof(tlf_ekf) <= 440);
    // The source itself never names an allocator (checked on the file, so a
    // future edit that adds one fails here).
    QFile src(QStringLiteral(TLF_RUNTIME_SOURCE));
    QVERIFY2(src.open(QIODevice::ReadOnly), TLF_RUNTIME_SOURCE);
    const QByteArray code = src.readAll();
    for (const char* banned : {"malloc", "calloc", "realloc", "free(", "stdio", "stdlib"})
        QVERIFY2(!code.contains(banned), banned);
}

QTEST_GUILESS_MAIN(TestFilters)
#include "test_filters.moc"
