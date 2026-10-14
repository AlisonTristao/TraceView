#include <tracelab.h>

/* The Furuta pendulum (rotary inverted pendulum): a horizontal arm driven by
 * a motor (theta1) and a pendulum free to swing in the vertical plane at the
 * arm's tip (theta2 = 0 upright, the unstable equilibrium). The model of
 * "Fast qLPV NMPC using CHRONOS: Parameter Trajectory Estimation and
 * Application to the Furuta Pendulum" (Tristao, Sanches, Medero, Morato),
 * Euler-Lagrange with rigid links, direct torque and no electrical dynamics:
 *
 *     M(q) q'' = b u - F q' - N(q, q') q' - d(q) + [0, tau_d]
 *
 *     M = [ alpha + beta sin^2 th2    gamma cos th2 ]     b = [1, 0]
 *         [ gamma cos th2             delta         ]     F = diag(mu1, mu2)
 *     N = [ beta th2' sin 2th2          -gamma th2' sin th2 ]
 *         [ -1/2 beta th1' sin 2th2      0                  ]
 *     d = [0, -eta sin th2]
 *
 *     alpha = I1 + m2 L1^2, beta = m2 l2^2, gamma = m2 L1 l2,
 *     delta = I2 + m2 l2^2, eta = m2 g l2
 *
 * theta1 turns counterclockwise seen from above; theta2 > 0 tilts the
 * pendulum toward increasing theta1. Integrated by the plant's solver (RK4
 * in the benchmark) at the sample rate of its card.
 *
 * DISTURBANCE tau_d on the pendulum's joint: sim.disturb (the scene's P / O
 * keys, % of disturb_max) plus a pulse of kick_torque for kick_duration at
 * kick_time (the paper's scenario; kick_time = 0 turns it off).
 *
 * SENSORS (loop.state): the paper measures the whole state, y = x. With
 * encoder_ticks > 0 the angles are counted in whole ticks and the rates are
 * the count's change over Ts; noise adds white noise to all four.
 */

/* The pendulum (SI units). */
TL_PARAM(m2, 0.09, "kg", 0.001, 10);         /* pendulum mass */
TL_PARAM(L1, 0.085, "m", 0.01, 2);           /* arm length */
TL_PARAM(l2, 0.13, "m", 0.01, 2);            /* joint to the pendulum's centre of mass */
TL_PARAM(I1, 0.005, "kg m^2", 1e-6, 1);      /* arm inertia */
TL_PARAM(I2, 5.07e-4, "kg m^2", 1e-7, 1);    /* pendulum inertia about its centre of mass */
TL_PARAM(mu1, 0.001, "N m s/rad", 0, 1);     /* viscous friction, arm */
TL_PARAM(mu2, 0.001, "N m s/rad", 0, 1);     /* viscous friction, pendulum */
TL_PARAM(g, 9.81, "m/s^2", 0, 100);
TL_PARAM(theta2_start, M_PI, "rad", -10, 10); /* pi: hanging down, at rest */

/* The disturbance on the pendulum's joint. */
TL_PARAM(disturb_max, 0.05, "N m", 0, 10);    /* sim.disturb = 100 % */
TL_PARAM(kick_time, 20, "s", 0, 1e6);
TL_PARAM(kick_torque, 0.02, "N m", -10, 10);
TL_PARAM(kick_duration, 0.1, "s", 0, 10);

/* The sensors. */
TL_PARAM_INT(encoder_ticks, 0, 0, 10000000);  /* per revolution; 0: exact */
TL_PARAM_BOOL(noise, 0);
TL_PARAM(angle_noise, 1e-3, "rad", 0, 1);
TL_PARAM(rate_noise, 1e-2, "rad/s", 0, 10);

TL_INPUT(u, "N m", -0.6, 0.6);               /* torque on the arm */

TL_STATE(theta1, "rad", 0);
TL_STATE(theta2, "rad", 0);                  /* set to theta2_start by plant_reset() */
TL_STATE(dtheta1, "rad/s", 0);
TL_STATE(dtheta2, "rad/s", 0);

TL_OUTPUT(th1, "rad");                       /* measured arm angle */
TL_OUTPUT(th2, "rad");                       /* measured pendulum angle (not wrapped) */
TL_OUTPUT(w1, "rad/s");                      /* measured arm rate */
TL_OUTPUT(w2, "rad/s");                      /* measured pendulum rate */

TL_DIAG(theta2_w, "rad");                    /* theta2 wrapped to (-pi, pi], for the charts */
TL_DIAG(tau_d, "N m");                       /* disturbance applied */
TL_DIAG(energy, "J");                        /* pendulum energy, 0 at rest upright */

static long long count[2];

static double counted(double angle) {
    double per_tick = 2.0 * M_PI / encoder_ticks;
    return per_tick * floor(angle / per_tick);
}

void plant_reset(void) {
    theta2 = theta2_start;
    count[0] = count[1] = 0;
    tau_d = 0;
    energy = 0;
    theta2_w = tl_wrap_angle(theta2);
}

void plant_deriv(void) {
    double alpha = I1 + m2 * L1 * L1;
    double beta = m2 * l2 * l2;
    double gamma = m2 * L1 * l2;
    double delta = I2 + m2 * l2 * l2;
    double eta = m2 * g * l2;

    double s = sin(theta2), c = cos(theta2), s2 = sin(2 * theta2);
    double t = tl_time();
    double kick = (kick_time > 0 && t >= kick_time && t < kick_time + kick_duration) ? kick_torque : 0;
    tau_d = disturb_max * tl_disturb(0) / 100.0 + kick;

    /* Right-hand side: b u - F q' - N q' - d(q) + [0, tau_d] */
    double f1 = u - mu1 * dtheta1 - beta * s2 * dtheta1 * dtheta2 + gamma * s * dtheta2 * dtheta2;
    double f2 = -mu2 * dtheta2 + 0.5 * beta * s2 * dtheta1 * dtheta1 + eta * s + tau_d;

    /* q'' = M^-1 f, D = det M > 0 */
    double m11 = alpha + beta * s * s, m12 = gamma * c;
    double det = m11 * delta - m12 * m12;
    der(theta1) = dtheta1;
    der(theta2) = dtheta2;
    der(dtheta1) = (delta * f1 - m12 * f2) / det;
    der(dtheta2) = (m11 * f2 - m12 * f1) / det;
}

void plant_output(void) {
    double angle[2] = {theta1, theta2};
    double rate[2] = {dtheta1, dtheta2};
    if (encoder_ticks > 0) {
        double per_tick = 2.0 * M_PI / encoder_ticks;
        for (int i = 0; i < 2; i++) {
            long long k = (long long)floor(angle[i] / per_tick);
            rate[i] = tl_tick() > 0 ? (double)(k - count[i]) * per_tick / tl_ts() : 0.0;
            count[i] = k;
            angle[i] = counted(angle[i]);
        }
    }
    if (noise) {
        for (int i = 0; i < 2; i++) {
            angle[i] += tl_noise(angle_noise);
            rate[i] += tl_noise(rate_noise);
        }
    }
    th1 = angle[0];
    th2 = angle[1];
    w1 = rate[0];
    w2 = rate[1];

    theta2_w = tl_wrap_angle(theta2);
    /* The pendulum's energy about the upright: 0 there, -2 eta hanging. */
    double delta = I2 + m2 * l2 * l2;
    energy = 0.5 * delta * dtheta2 * dtheta2 + m2 * g * l2 * (cos(theta2) - 1);
}
