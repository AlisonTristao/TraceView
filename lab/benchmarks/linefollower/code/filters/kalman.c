#include <tracelab.h>

/* Extended Kalman filter for the line follower's speed and turn rate, from
 * the encoders, the gyro and the accelerometer.
 *
 * STATE   x = [v, omega, bg, ba]
 *     v      forward speed (m/s)
 *     omega  turn rate (rad/s, > 0 turns right: the car's convention)
 *     bg     the gyro's bias (rad/s)
 *     ba     the accelerometer's bias along the car (m/s^2)
 *
 * PREDICTION, the accelerometer along the car as the input (it is the
 * speed's change over the step, dv/dt):
 *     v(k)     = v(k-1) + Ts (accel_x(k) - ba)
 *     omega(k) = omega(k-1)          (a random walk: q_omega)
 *     bg, ba   constant              (random walks: the sensors' bias walks)
 *
 * CORRECTION, one measurement at a time (their noises are independent, so
 * this is the same as the joint update and needs no matrix inverse):
 *     w_l     = (v + omega L/2) / r          encoders, whole ticks
 *     w_r     = (v - omega L/2) / r
 *     gyro    = omega + bg
 *     accel_y = v omega                      the centripetal acceleration
 *                                            (nonlinear: linearized at x)
 *
 * The bias states are what the wheels and the gyro (and the integrated
 * accelerometer and the wheels) disagree on, so they settle to the sensors'
 * random-walk biases. r and L must be the car's (the plant's wheel_radius
 * and wheel_distance); the noises default to the car's with its `noise` on
 * at 80 Hz. The estimates go on as new signals, v_est and omega_est, for a
 * controller to read, and are published for the charts (ctl.diag) with the
 * biases and the speed's standard deviation.
 */

TL_PARAM(wheel_radius, 0.04, "m", 0.001, 1);
TL_PARAM(wheel_distance, 0.15, "m", 0.01, 2);
TL_PARAM(enc_noise, 0.15, "rad/s", 1e-6, 100);       /* one tick over Ts, / sqrt(12) */
TL_PARAM(gyro_noise, 0.02, "rad/s", 1e-6, 10);
TL_PARAM(accel_noise, 0.05, "m/s^2", 1e-6, 100);
TL_PARAM(gyro_walk, 0.002, "rad/s/sqrt(s)", 0, 10);
TL_PARAM(accel_walk, 0.005, "m/s^2/sqrt(s)", 0, 100);
TL_PARAM(omega_accel, 30, "rad/s^2", 0, 1000);       /* how fast omega may change */
TL_PARAM_BOOL(use_accel_y, 1);

TL_IN(w_l);
TL_IN(w_r);
TL_IN(gyro);
TL_IN(accel_x);
TL_IN(accel_y);

TL_OUT(v_est);
TL_OUT(omega_est);

TL_DIAG(v_kf, "m/s");
TL_DIAG(omega_kf, "rad/s");
TL_DIAG(gyro_bias_kf, "rad/s");
TL_DIAG(accel_bias_kf, "m/s^2");
TL_DIAG(v_sigma, "m/s");

#define NX 4

static double x[NX];
static double P[NX][NX];
static int primed;

void filt_reset(void) {
    for (int i = 0; i < NX; i++) {
        x[i] = 0;
        for (int j = 0; j < NX; j++) P[i][j] = 0;
    }
    primed = 0;
}

/* The first sample: the speed and turn rate the wheels say, no bias yet. */
static void start(void) {
    double r = wheel_radius, L = wheel_distance;
    x[0] = r * (w_l + w_r) / 2;
    x[1] = r * (w_l - w_r) / L;
    x[2] = 0;
    x[3] = 0;
    for (int i = 0; i < NX; i++)
        for (int j = 0; j < NX; j++) P[i][j] = 0;
    P[0][0] = 0.1 * 0.1;
    P[1][1] = 1.0;
    P[2][2] = 0.05 * 0.05;
    P[3][3] = 0.5 * 0.5;
    primed = 1;
}

static void predict(double ts) {
    /* x = f(x, accel_x); F = df/dx has one off-diagonal term, dv/dba = -Ts. */
    x[0] += ts * (accel_x - x[3]);
    /* P = F P F' + Q, with F = I + E, E[0][3] = -ts. */
    double FP[NX][NX];
    for (int j = 0; j < NX; j++) {
        for (int i = 0; i < NX; i++) FP[i][j] = P[i][j];
        FP[0][j] -= ts * P[3][j];
    }
    for (int i = 0; i < NX; i++) {
        for (int j = 0; j < NX; j++) P[i][j] = FP[i][j];
        P[i][0] -= ts * FP[i][3];
    }
    double qv = ts * accel_noise;
    double qw = ts * omega_accel;
    P[0][0] += qv * qv;
    P[1][1] += qw * qw;
    P[2][2] += gyro_walk * gyro_walk * ts;
    P[3][3] += accel_walk * accel_walk * ts;
}

/* One scalar measurement z = h(x) + noise of variance R, H = dh/dx. */
static void update(double z, double h, const double H[NX], double R) {
    double PH[NX];
    double S = R;
    for (int i = 0; i < NX; i++) {
        PH[i] = 0;
        for (int j = 0; j < NX; j++) PH[i] += P[i][j] * H[j];
        S += H[i] * PH[i];
    }
    if (!(S > 0)) return;
    double innovation = z - h;
    for (int i = 0; i < NX; i++) x[i] += PH[i] / S * innovation;
    /* P = P - K H P = P - PH PH' / S, kept symmetric. */
    for (int i = 0; i < NX; i++)
        for (int j = 0; j < NX; j++) P[i][j] -= PH[i] * PH[j] / S;
    for (int i = 0; i < NX; i++)
        for (int j = i + 1; j < NX; j++) P[i][j] = P[j][i] = 0.5 * (P[i][j] + P[j][i]);
}

void filt_step(void) {
    double r = wheel_radius, L = wheel_distance;
    if (!primed) {
        start();
    } else {
        predict(tl_ts());
    }

    double He[NX] = {1 / r, L / (2 * r), 0, 0};
    update(w_l, (x[0] + x[1] * L / 2) / r, He, enc_noise * enc_noise);
    He[1] = -L / (2 * r);
    update(w_r, (x[0] - x[1] * L / 2) / r, He, enc_noise * enc_noise);

    double Hg[NX] = {0, 1, 1, 0};
    update(gyro, x[1] + x[2], Hg, gyro_noise * gyro_noise);

    if (use_accel_y) {
        double Ha[NX] = {x[1], x[0], 0, 0};
        update(accel_y, x[0] * x[1], Ha, accel_noise * accel_noise);
    }

    v_est = v_kf = x[0];
    omega_est = omega_kf = x[1];
    gyro_bias_kf = x[2];
    accel_bias_kf = x[3];
    v_sigma = sqrt(P[0][0] > 0 ? P[0][0] : 0);
}
