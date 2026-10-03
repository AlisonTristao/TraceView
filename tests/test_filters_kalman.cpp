// The Kalman filters of the C filter runtime (lab/filters/runtime/tlfilter.h),
// built in float as the firmware builds them (tracelab_filters_rt): one
// update against the textbook formulas computed independently in double
// (with a 2-row measurement, so the elimination and its pivoting are
// exercised), a singular innovation covariance skipping the measurement, a
// constant-velocity model converging with its covariance and gain equal to
// the steady-state discrete Riccati solution, the chain block with a known
// input, the EKF reducing to the KF on a linear model, and the EKF tracking
// a pendulum through a nonlinear measurement, with its context pointer.

#include <QtTest>
#include <cmath>
#include <random>

#include "lab/filters/runtime/tlfilter.h"

namespace {

using M2 = double[2][2];

// The steady-state discrete Riccati solution of the constant-velocity model
// with a position measurement, iterated in double with the textbook
// (non-Joseph) recursion -- an independent implementation of what the
// filter's covariance must converge to. Returns the posterior (after-update)
// covariance and the gain.
void steadyState(double dt, double q, double r, M2 Ppost, double K[2]) {
    const double F[2][2] = {{1, dt}, {0, 1}};
    const double Q[2][2] = {{q * dt * dt * dt / 3, q * dt * dt / 2}, {q * dt * dt / 2, q * dt}};
    double P[2][2] = {{1, 0}, {0, 1}};  // posterior
    for (int it = 0; it < 200000; ++it) {
        double T[2][2], Pp[2][2];
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) T[i][j] = F[i][0] * P[0][j] + F[i][1] * P[1][j];
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) Pp[i][j] = T[i][0] * F[j][0] + T[i][1] * F[j][1] + Q[i][j];
        const double S = Pp[0][0] + r;
        const double k0 = Pp[0][0] / S, k1 = Pp[1][0] / S;
        double Pn[2][2] = {{Pp[0][0] - k0 * Pp[0][0], Pp[0][1] - k0 * Pp[0][1]},
                           {Pp[1][0] - k1 * Pp[0][0], Pp[1][1] - k1 * Pp[0][1]}};
        double diff = 0;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                diff = std::max(diff, std::abs(Pn[i][j] - P[i][j]));
                P[i][j] = Pn[i][j];
            }
        K[0] = k0;
        K[1] = k1;
        if (diff < 1e-18) break;
    }
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) Ppost[i][j] = P[i][j];
}

void setupCv(tlf_kf& kf, float dt, float q, float r) {
    QCOMPARE(tlf_kf_init(&kf, 2, 1, 1), int(TLF_OK));
    kf.F[0][1] = dt;
    kf.B[0][0] = 0.5f * dt * dt;
    kf.B[1][0] = dt;
    kf.H[0][0] = 1;
    kf.Q[0][0] = q * dt * dt * dt / 3;
    kf.Q[0][1] = kf.Q[1][0] = q * dt * dt / 2;
    kf.Q[1][1] = q * dt;
    kf.R[0][0] = r;
    kf.P[0][0] = kf.P[1][1] = 100;
}

bool relClose(double a, double b, double tol) {
    return std::abs(a - b) <= tol * std::max(std::abs(b), 1e-12);
}

// The pendulum of the EKF test: theta'' = -(g/L) sin(theta) + u, Euler at dt;
// measured: the bob's horizontal position L sin(theta).
struct Pendulum {
    float dt = 0.01f;
    float gOverL = 9.81f;
    float length = 0.5f;
    int fCalls = 0;
    int hCalls = 0;
};

void pendF(void* ctx, const tlf_real* x, const tlf_real* u, tlf_real* xn,
           tlf_real F[][TLF_KF_MAX_N]) {
    auto* p = static_cast<Pendulum*>(ctx);
    ++p->fCalls;
    xn[0] = x[0] + p->dt * x[1];
    xn[1] = x[1] + p->dt * (-p->gOverL * std::sin(x[0]) + u[0]);
    F[0][0] = 1;
    F[0][1] = p->dt;
    F[1][0] = -p->dt * p->gOverL * std::cos(x[0]);
    F[1][1] = 1;
}

void pendH(void* ctx, const tlf_real* x, const tlf_real*, tlf_real* z, tlf_real H[][TLF_KF_MAX_N]) {
    auto* p = static_cast<Pendulum*>(ctx);
    ++p->hCalls;
    z[0] = p->length * std::sin(x[0]);
    H[0][0] = p->length * std::cos(x[0]);
    H[0][1] = 0;
}

// A linear model behind the EKF callbacks (F, H from a tlf_kf).
void linF(void* ctx, const tlf_real* x, const tlf_real* u, tlf_real* xn,
          tlf_real F[][TLF_KF_MAX_N]) {
    const tlf_kf* kf = static_cast<const tlf_kf*>(ctx);
    for (int i = 0; i < kf->n; ++i) {
        tlf_real acc = 0;
        for (int j = 0; j < kf->n; ++j) acc += kf->F[i][j] * x[j], F[i][j] = kf->F[i][j];
        for (int j = 0; j < kf->nu; ++j) acc += kf->B[i][j] * u[j];
        xn[i] = acc;
    }
}
void linH(void* ctx, const tlf_real* x, const tlf_real*, tlf_real* z, tlf_real H[][TLF_KF_MAX_N]) {
    const tlf_kf* kf = static_cast<const tlf_kf*>(ctx);
    for (int i = 0; i < kf->m; ++i) {
        tlf_real acc = 0;
        for (int j = 0; j < kf->n; ++j) acc += kf->H[i][j] * x[j], H[i][j] = kf->H[i][j];
        z[i] = acc;
    }
}

}  // namespace

class TestFiltersKalman : public QObject {
    Q_OBJECT

private slots:
    void updateMatchesTextbook();
    void singularInnovationSkipsMeasurement();
    void constantVelocityConvergesToRiccati();
    void chainBlockWithInput();
    void ekfReducesToKf();
    void ekfTracksPendulum();
};

void TestFiltersKalman::updateMatchesTextbook() {
    // n = 3, m = 2, an arbitrary SPD P and a H whose first column is small,
    // so the elimination has to pivot.
    tlf_kf kf;
    QCOMPARE(tlf_kf_init(&kf, 3, 2, 0), int(TLF_OK));
    const double P[3][3] = {{2.0, 0.3, -0.2}, {0.3, 1.5, 0.4}, {-0.2, 0.4, 1.0}};
    const double H[2][3] = {{0.01, 1.0, 0.0}, {1.0, 0.0, 2.0}};
    const double R[2][2] = {{0.5, 0.1}, {0.1, 0.3}};
    const double x[3] = {1.0, -2.0, 0.5};
    const double z[2] = {-1.5, 3.0};
    for (int i = 0; i < 3; ++i) {
        kf.x[i] = float(x[i]);
        for (int j = 0; j < 3; ++j) kf.P[i][j] = float(P[i][j]);
    }
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j) kf.H[i][j] = float(H[i][j]);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) kf.R[i][j] = float(R[i][j]);
    const float zf[2] = {float(z[0]), float(z[1])};
    QCOMPARE(tlf_kf_update(&kf, zf), int(TLF_OK));

    // Textbook, in double: S = HPH' + R, K = PH'S^-1, x += K(z - Hx),
    // P -= K H P.
    double PHt[3][2] = {}, S[2][2] = {}, K[3][2] = {}, y[2] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 3; ++k) PHt[i][j] += P[i][k] * H[j][k];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            S[i][j] = R[i][j];
            for (int k = 0; k < 3; ++k) S[i][j] += H[i][k] * PHt[k][j];
        }
    const double det = S[0][0] * S[1][1] - S[0][1] * S[1][0];
    const double Si[2][2] = {{S[1][1] / det, -S[0][1] / det}, {-S[1][0] / det, S[0][0] / det}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j) K[i][j] = PHt[i][0] * Si[0][j] + PHt[i][1] * Si[1][j];
    for (int i = 0; i < 2; ++i) {
        y[i] = z[i];
        for (int k = 0; k < 3; ++k) y[i] -= H[i][k] * x[k];
        QVERIFY(relClose(kf.y[i], y[i], 1e-5));
    }
    for (int i = 0; i < 3; ++i) {
        const double xi = x[i] + K[i][0] * y[0] + K[i][1] * y[1];
        QVERIFY2(std::abs(kf.x[i] - xi) < 1e-5,
                 qPrintable(QString("x[%1] %2 vs %3").arg(i).arg(kf.x[i]).arg(xi)));
        for (int j = 0; j < 3; ++j) {
            double pij = P[i][j];
            for (int k = 0; k < 2; ++k) pij -= K[i][k] * PHt[j][k];  // (K H P)_ij = K_i. (P H')_j.'
            QVERIFY2(std::abs(kf.P[i][j] - pij) < 1e-5,
                     qPrintable(QString("P[%1][%2]").arg(i).arg(j)));
            QCOMPARE(kf.P[i][j], kf.P[j][i]);  // symmetric by construction
        }
    }
}

void TestFiltersKalman::singularInnovationSkipsMeasurement() {
    tlf_kf kf;
    QCOMPARE(tlf_kf_init(&kf, 2, 2, 0), int(TLF_OK));
    // Two identical measurement rows, no noise, P singular along them: S = 0.
    kf.H[0][0] = kf.H[1][0] = 1;
    kf.P[0][0] = 0;
    kf.x[0] = 3;
    tlf_kf before = kf;
    const float z[2] = {1, 2};
    QCOMPARE(tlf_kf_update(&kf, z), int(TLF_ESINGULAR));
    QCOMPARE(kf.x[0], before.x[0]);
    QCOMPARE(kf.P[1][1], before.P[1][1]);
    // Rank-deficient but nonzero S is refused too.
    kf.P[0][0] = 1;
    QCOMPARE(tlf_kf_update(&kf, z), int(TLF_ESINGULAR));
    QCOMPARE(tlf_kf_init(&kf, TLF_KF_MAX_N + 1, 1, 0), int(TLF_EINVAL));
    QCOMPARE(tlf_kf_init(&kf, 2, 0, 0), int(TLF_EINVAL));
}

void TestFiltersKalman::constantVelocityConvergesToRiccati() {
    const float dt = 0.01f, q = 0.5f, r = 0.04f;  // measurement sigma 0.2
    tlf_kf kf;
    setupCv(kf, dt, q, r);

    std::mt19937 rng(5);
    std::normal_distribution<double> wAcc(0.0, std::sqrt(q / dt)), vMeas(0.0, std::sqrt(r));
    double pos = 0.0, vel = 1.5;
    double sqErrPos = 0, sqErrMeas = 0, sqErrVel = 0;
    int n = 0;
    for (int k = 0; k < 5000; ++k) {
        const double a = wAcc(rng);
        pos += vel * dt + 0.5 * a * dt * dt;
        vel += a * dt;
        const float z = float(pos + vMeas(rng));
        tlf_kf_predict(&kf, nullptr);
        QCOMPARE(tlf_kf_update(&kf, &z), int(TLF_OK));
        if (k > 1000) {
            sqErrPos += (kf.x[0] - pos) * (kf.x[0] - pos);
            sqErrVel += (kf.x[1] - vel) * (kf.x[1] - vel);
            sqErrMeas += (z - pos) * (z - pos);
            ++n;
        }
    }
    // Converged: far better than the raw measurement, the velocity found.
    QVERIFY(std::sqrt(sqErrPos / n) < 0.5 * std::sqrt(sqErrMeas / n));

    double Pss[2][2], Kss[2];
    steadyState(dt, q, r, Pss, Kss);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            QVERIFY2(relClose(kf.P[i][j], Pss[i][j], 1e-3),
                     qPrintable(QString("P[%1][%2] %3 vs %4")
                                    .arg(i).arg(j).arg(kf.P[i][j]).arg(Pss[i][j])));
    // The empirical errors agree with that covariance (within sampling error).
    QVERIFY(relClose(sqErrPos / n, Pss[0][0], 0.2));
    QVERIFY(relClose(sqErrVel / n, Pss[1][1], 0.3));
    // And the gain: K = P_post H' / R for the posterior covariance.
    QVERIFY(relClose(kf.P[0][0] / r, Kss[0], 1e-3));
    QVERIFY(relClose(kf.P[1][0] / r, Kss[1], 1e-3));
}

void TestFiltersKalman::chainBlockWithInput() {
    // Constant known acceleration as the input, position measured without
    // noise: the chain block tracks position and velocity.
    const float dt = 0.01f;
    tlf_kf kf;
    setupCv(kf, dt, 1e-4f, 1e-4f);
    kf.u[0] = 2.0f;
    tlf_block blocks[1] = {tlf_kf_block(&kf)};
    QCOMPARE(int(blocks[0].n_in), 1);
    QCOMPARE(int(blocks[0].n_out), 2);
    tlf_chain chain;
    QCOMPARE(tlf_chain_init(&chain, blocks, 1), int(TLF_OK));
    float out[2] = {0, 0};
    double t = 0;
    for (int k = 1; k <= 500; ++k) {
        t = k * dt;
        const float z = float(1.0 + 0.5 * 2.0 * t * t);  // v0 = 0, a = 2
        tlf_chain_step(&chain, &z, out);
    }
    QVERIFY2(std::abs(out[0] - (1.0 + t * t)) < 1e-3, qPrintable(QString::number(out[0])));
    QVERIFY2(std::abs(out[1] - 2.0 * t) < 2e-2, qPrintable(QString::number(out[1])));
    tlf_chain_reset(&chain);
    QCOMPARE(kf.x[0], 0.0f);
    QCOMPARE(kf.P[0][0], 1.0f);
}

void TestFiltersKalman::ekfReducesToKf() {
    tlf_kf kf;
    setupCv(kf, 0.02f, 0.3f, 0.1f);
    tlf_kf model = kf;  // F, B, H for the callbacks
    tlf_ekf ekf;
    QCOMPARE(tlf_ekf_init(&ekf, 2, 1, 1, linF, linH, &model), int(TLF_OK));
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) ekf.P[i][j] = kf.P[i][j], ekf.Q[i][j] = kf.Q[i][j];
    ekf.R[0][0] = kf.R[0][0];
    std::mt19937 rng(9);
    std::normal_distribution<float> noise(0.0f, 0.3f);
    for (int k = 0; k < 300; ++k) {
        const float u = std::sin(0.05f * k);
        const float z = 0.1f * k + noise(rng);
        tlf_kf_predict(&kf, &u);
        tlf_ekf_predict(&ekf, &u);
        QCOMPARE(tlf_kf_update(&kf, &z), int(TLF_OK));
        QCOMPARE(tlf_ekf_update(&ekf, &z, &u), int(TLF_OK));
        for (int i = 0; i < 2; ++i) {
            QVERIFY(std::abs(kf.x[i] - ekf.x[i]) < 1e-4f);
            for (int j = 0; j < 2; ++j) QVERIFY(std::abs(kf.P[i][j] - ekf.P[i][j]) < 1e-6f);
        }
    }
}

void TestFiltersKalman::ekfTracksPendulum() {
    Pendulum pend;
    tlf_ekf ekf;
    QCOMPARE(tlf_ekf_init(&ekf, 2, 1, 1, pendF, pendH, &pend), int(TLF_OK));
    ekf.Q[0][0] = 1e-6f;
    ekf.Q[1][1] = 1e-4f;
    const float sigma = 0.01f;  // m on the bob's horizontal position
    ekf.R[0][0] = sigma * sigma;
    ekf.x[0] = 0.3f;  // wrong initial guess: the truth starts at 0.8 rad
    ekf.x[1] = 0.0f;
    ekf.P[0][0] = 0.5f;
    ekf.P[1][1] = 1.0f;

    std::mt19937 rng(21);
    std::normal_distribution<double> vMeas(0.0, sigma);
    double th = 0.8, om = 0.0;
    double maxErr = 0, sqErr = 0;
    int n = 0;
    const int steps = 1500;  // 15 s
    for (int k = 0; k < steps; ++k) {
        const float u = 0.5f * std::sin(0.7f * k * pend.dt);  // a known torque
        // Truth with the same Euler step as the model.
        const double thn = th + pend.dt * om;
        om = om + pend.dt * (-pend.gOverL * std::sin(th) + u);
        th = thn;
        const float z = float(pend.length * std::sin(th) + vMeas(rng));
        tlf_ekf_predict(&ekf, &u);
        QCOMPARE(tlf_ekf_update(&ekf, &z, &u), int(TLF_OK));
        if (k > steps / 2) {
            maxErr = std::max(maxErr, std::abs(ekf.x[0] - th));
            sqErr += (ekf.x[0] - th) * (ekf.x[0] - th);
            ++n;
        }
    }
    QVERIFY2(maxErr < 0.06, qPrintable(QString::number(maxErr)));
    // Better than inverting one noisy measurement (sigma / L ~ 0.02 rad).
    QVERIFY(std::sqrt(sqErr / n) < 0.5 * sigma / pend.length);
    QCOMPARE(pend.fCalls, steps);
    QCOMPARE(pend.hCalls, steps);
    QCOMPARE(tlf_ekf_init(&ekf, 2, 1, 1, nullptr, pendH, &pend), int(TLF_EINVAL));
}

QTEST_GUILESS_MAIN(TestFiltersKalman)
#include "test_filters_kalman.moc"
