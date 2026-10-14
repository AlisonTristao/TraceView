#include <tracelab.h>

/* Energy swing-up + LQR: the classic Furuta pendulum controller, a baseline
 * for the qLPV MPC.
 *
 * SWING-UP (Astrom & Furuta). The pendulum's energy about the upright,
 *     E = 1/2 delta w2^2 + eta (cos th2 - 1)      (0 upright at rest)
 * changes with the arm's acceleration as dE/dt = -gamma cos(th2) w2 th1'' +
 * friction, so pushing the arm against w2 cos(th2) pumps energy in while
 * E < 0 and takes it out past the top:
 *     u = sat(k_energy E, u_swing) sgn(w2 cos th2) - k_arm (th1 - r) - d_arm w1
 * (the last terms keep the arm near its reference while it pumps).
 *
 * BALANCE. Within `catch_angle` of the upright, an LQR on the Euler
 * discretization of the model linearized there (Ts = the plant's period),
 * u = -K (x - r), K from the discrete Riccati equation solved here whenever
 * a parameter changes. It lets go beyond `release_angle`.
 */

TL_PARAM(k_energy, 20, "N m/J", 0, 1e4);
TL_PARAM(u_swing, 0.3, "N m", 0, 10);
TL_PARAM(k_arm, 0.05, "N m/rad", 0, 10);
TL_PARAM(d_arm, 0.01, "N m s/rad", 0, 10);
TL_PARAM(catch_angle, 0.35, "rad", 0.01, M_PI);
TL_PARAM(release_angle, 0.8, "rad", 0.01, M_PI);
TL_PARAM(q_theta1, 10, "", 0, 1e9);
TL_PARAM(q_theta2, 100, "", 0, 1e9);
TL_PARAM(q_dtheta1, 1, "", 0, 1e9);
TL_PARAM(q_dtheta2, 1, "", 0, 1e9);
TL_PARAM(r_input, 10, "", 1e-9, 1e9);
TL_PARAM(u_max, 0.6, "N m", 0, 100);

/* The reference: th1 = theta1_ref (+ step after step_time). */
TL_PARAM(theta1_ref, 0, "rad", -100, 100);
TL_PARAM(step, M_PI, "rad", -100, 100);
TL_PARAM(step_time, 11, "s", 0, 1e6);         /* 0: no step */

/* The pendulum the model is built from (the plant's defaults). */
TL_PARAM(m2, 0.09, "kg", 0.001, 10);
TL_PARAM(L1, 0.085, "m", 0.01, 2);
TL_PARAM(l2, 0.13, "m", 0.01, 2);
TL_PARAM(I1, 0.005, "kg m^2", 1e-6, 1);
TL_PARAM(I2, 5.07e-4, "kg m^2", 1e-7, 1);
TL_PARAM(mu1, 0.001, "N m s/rad", 0, 1);
TL_PARAM(mu2, 0.001, "N m s/rad", 0, 1);
TL_PARAM(g, 9.81, "m/s^2", 0, 100);

TL_IN(th1);
TL_IN(th2);
TL_IN(w1);
TL_IN(w2);
TL_OUT(u);

TL_DIAG(theta1_r, "rad");
TL_DIAG(theta2_w, "rad");     /* pendulum angle, wrapped to (-pi, pi] */
TL_DIAG(energy_est, "J");     /* E from the measurements */
TL_DIAG(balancing, "");       /* 1: LQR, 0: swing-up */

#define NX 4

static double K[NX];
static double key_seen;
static int lqr_on;

/* K of the discrete LQR, iterating the Riccati equation in Joseph form,
   P = Q + K'RK + (A - BK)' P (A - BK), K = (R + B'PB)^-1 B'PA,
   which keeps P symmetric and the iteration stable. */
static void design_lqr(void) {
    double ts = tl_ts();
    double alpha = I1 + m2 * L1 * L1;
    double gamma = m2 * L1 * l2, delta = I2 + m2 * l2 * l2, eta = m2 * g * l2;
    double det = alpha * delta - gamma * gamma;
    /* q'' = M0^-1 ([u - mu1 w1, -mu2 w2 + eta th2]), M0 = [alpha gamma; gamma delta] */
    double A[NX][NX] = {{1, 0, ts, 0}, {0, 1, 0, ts}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    A[2][1] = ts * (-gamma * eta) / det;
    A[2][2] += ts * (-delta * mu1) / det;
    A[2][3] = ts * (gamma * mu2) / det;
    A[3][1] = ts * (alpha * eta) / det;
    A[3][2] = ts * (gamma * mu1) / det;
    A[3][3] += ts * (-alpha * mu2) / det;
    double B[NX] = {0, 0, ts * delta / det, ts * (-gamma) / det};
    double Q[NX] = {q_theta1, q_theta2, q_dtheta1, q_dtheta2};

    double P[NX][NX], Acl[NX][NX], PA[NX][NX], next[NX][NX], PB[NX];
    memset(P, 0, sizeof P);
    for (int i = 0; i < NX; i++) P[i][i] = Q[i];
    for (int it = 0; it < 20000; it++) {
        double s = r_input;
        for (int i = 0; i < NX; i++) {
            PB[i] = 0;
            for (int k = 0; k < NX; k++) PB[i] += P[i][k] * B[k];
            s += B[i] * PB[i];
        }
        for (int j = 0; j < NX; j++) {
            double bpa = 0;
            for (int i = 0; i < NX; i++) bpa += PB[i] * A[i][j];
            K[j] = bpa / s;
        }
        for (int i = 0; i < NX; i++)
            for (int j = 0; j < NX; j++) Acl[i][j] = A[i][j] - B[i] * K[j];
        for (int i = 0; i < NX; i++)
            for (int j = 0; j < NX; j++) {
                double sum = 0;
                for (int k = 0; k < NX; k++) sum += P[i][k] * Acl[k][j];
                PA[i][j] = sum;
            }
        double change = 0, size = 0;
        for (int i = 0; i < NX; i++)
            for (int j = 0; j <= i; j++) {
                double sum = (i == j ? Q[i] : 0) + r_input * K[i] * K[j];
                for (int k = 0; k < NX; k++) sum += Acl[k][i] * PA[k][j];
                next[i][j] = next[j][i] = sum;
                change = fmax(change, fabs(sum - P[i][j]));
                size = fmax(size, fabs(sum));
            }
        memcpy(P, next, sizeof P);
        if (change <= 1e-12 * size) break;
    }
}

void ctl_reset(void) {
    key_seen = 0;
    lqr_on = 0;
}

void ctl_step(void) {
    double key = tl_ts() + m2 + 3 * L1 + 5 * l2 + 7 * I1 + 11 * I2 + 13 * mu1 + 17 * mu2 +
                 19 * g + 23 * q_theta1 + 29 * q_theta2 + 31 * q_dtheta1 + 37 * q_dtheta2 +
                 41 * r_input;
    if (key != key_seen) {
        design_lqr();
        key_seen = key;
    }

    double t = tl_time();
    theta1_r = theta1_ref + ((step_time > 0 && t >= step_time) ? step : 0.0);
    theta2_w = tl_wrap_angle(th2);
    double delta = I2 + m2 * l2 * l2, eta = m2 * g * l2;
    energy_est = 0.5 * delta * w2 * w2 + eta * (cos(th2) - 1);

    if (fabs(theta2_w) < catch_angle) lqr_on = 1;
    if (fabs(theta2_w) > release_angle) lqr_on = 0;

    double v;
    if (lqr_on) {
        double e[NX] = {th1 - theta1_r, theta2_w, w1, w2};
        v = 0;
        for (int i = 0; i < NX; i++) v -= K[i] * e[i];
    } else {
        double pump = w2 * cos(th2);
        v = tl_sat(k_energy * energy_est, -u_swing, u_swing) * (pump >= 0 ? 1.0 : -1.0);
        v -= k_arm * (th1 - theta1_r) + d_arm * w1;
    }
    u = tl_sat(v, -u_max, u_max);
    balancing = lqr_on;
}
