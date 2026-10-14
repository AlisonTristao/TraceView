#include <tracelab.h>

/* qLPV MPC line follower (the port of line_follower_simulator's mpc/:
 * model.py, scheduling.py, qlpv_mpc.py and line_follower.py).
 *
 * MODEL. The exact qLPV embedding of the differential-drive car, written in
 * the car frame of the instant k:
 *     x = [w_l, w_r, theta, X, Y]   wheel speeds (rad/s), heading (rad),
 *                                   lateral and forward position (m)
 *     u = [u_l, u_r]                % of PWM
 *     w(k+1)     = a w(k) + b (u(k) + d)
 *     theta(k+1) = theta(k) + Ts r/L (w_l - w_r)(k+1)
 *     X(k+1)     = X(k) + v(k+1) Ts sin(theta(k))      v = r/2 (w_l + w_r)
 *     Y(k+1)     = Y(k) + v(k+1) Ts cos(theta(k))
 *     x(k+1) = A(rho) x(k) + B(rho) (u(k) + d),  A(rho) = A0 + rho1 A1 + rho2 A2
 * Two exact factorizations of v sin(theta):
 *     sinc:    rho = [cos(theta), v(k+1) sinc(theta)]   X linear in the heading
 *     sin_cos: rho = [sin(theta), cos(theta)]           X linear in the speed
 * theta > 0 turns right, X > 0 is to the right, Y forward.
 *
 * SCHEDULING. rho(k+j|k) = f_rho(x(k+j), x(k+j+1)) needs the state sequence
 * the QP is about to choose. Estimates:
 *     frozen      rho of now over the whole horizon
 *     warm_start  f_rho along the previous prediction
 *     taylor      first-order extrapolation around now
 *     iterative   solve, recompute rho from the prediction, solve again...
 *
 * MPC. For a given scheduling trajectory the prediction is linear in U and
 *     min  sum_j |y(k+j) - r(k+j)|^2_W(j) + |u|^2_R + |du|^2_Rd
 *     s.t. u_min <= u <= u_max
 * is a box QP (tl_qp_box). With contouring the position error is measured
 * along the track (lateral / longitudinal) instead of in X / Y.
 *
 * CONTROLLER. The reference comes from the preview's points (heading of the
 * track tangent); an input disturbance is estimated from the last wheel step
 * (offset-free tracking); the previous solution is carried into the new car
 * frame by the odometry shift.
 */

TL_PARAM_ENUM(embedding, 0, "sinc|sin_cos");
TL_PARAM_ENUM(scheduling, 2, "frozen|warm_start|taylor|iterative");
TL_PARAM_INT(iterations, 3, 1, 50);                   /* iterative: most QPs */
TL_PARAM(eps, 1e-3, "", 0, 1);                        /* iterative: rho settled */
TL_PARAM_ENUM(initial, 0, "frozen|warm_start|taylor"); /* iterative: first estimate */
TL_PARAM(relaxation, 1.0, "", 0.01, 2);               /* iterative: gamma */
TL_PARAM_BOOL(contouring, 1);
TL_PARAM(q_theta, 1e1, "", 0, 1e9);
TL_PARAM(q_lateral, 1e4, "", 0, 1e9);
TL_PARAM(q_longitudinal, 1e2, "", 0, 1e9);
TL_PARAM(qn_theta, 1e2, "", 0, 1e9);                  /* terminal */
TL_PARAM(qn_lateral, 1e5, "", 0, 1e9);
TL_PARAM(qn_longitudinal, 1e3, "", 0, 1e9);
TL_PARAM(weight_growth, 1.0, "", 0.01, 100);          /* last stage / first stage */
TL_PARAM(r_input, 0, "", 0, 1e3);                     /* |u|^2 */
TL_PARAM(rd_input, 1e-2, "", 0, 1e3);                 /* |du|^2 */
TL_PARAM(disturbance_filter, 0.2, "", 0, 1);
TL_PARAM(u_min, -100, "%", -100, 100);
TL_PARAM(u_max, 100, "%", -100, 100);

/* The car the model is built from (config.py "car" and "motors"). */
TL_PARAM(wheel_radius, 0.04, "m", 0.001, 1);
TL_PARAM(wheel_distance, 0.15, "m", 0.01, 2);
TL_PARAM(wheel_rpm, 1000, "rpm", 1, 1e5);
TL_PARAM(ke_left, 1, "", 0, 10);
TL_PARAM(ke_right, 1, "", 0, 10);
TL_PARAM(settle_left, 0.62, "s", 0.001, 10);          /* 5 tau */
TL_PARAM(settle_right, 0.58, "s", 0.001, 10);

#define NX 5
#define NU 2
#define NY 3                   /* outputs theta, X, Y = states 2, 3, 4 */
#define NMAX 64
#define NUMAX (NMAX * NU)

TL_IN(w_l);                    /* wheel speeds, rad/s */
TL_IN(w_r);
TL_PREVIEW(px, py, NMAX);      /* the track ahead, car frame */
TL_OUT(u_l);                   /* % */
TL_OUT(u_r);

TL_DIAG(qps, "");              /* QPs solved this step */
TL_DIAG(qp_iterations, "");    /* active-set iterations, all QPs */
TL_DIAG(rho_error, "");        /* max |f_rho(prediction) - rho used| */
TL_DIAG(theta_ref, "rad");     /* heading aimed at on the first step */
TL_DIAG(theta_pred, "rad");    /* heading predicted there */
TL_DIAG(w_l_filt, "rad/s");    /* the wheel speeds it was given (after the filters) */
TL_DIAG(w_r_filt, "rad/s");

/* ------------------------------------------------------------------ */
/* Model                                                               */

static double A_aff[3][NX][NX], B_aff[3][NX][NU];
static double alpha[2], beta[2], c_theta, c_v, rho_min[2], rho_max[2];

static double sinc(double t) {
    return t == 0.0 ? 1.0 : sin(t) / t;
}

static double dsinc(double t) {
    if (fabs(t) < 1e-4) return -t / 3.0;
    return (t * cos(t) - sin(t)) / (t * t);
}

static void build_model(void) {
    double ts = tl_ts();
    double ke = 2.0 * M_PI * wheel_rpm / 6000.0;
    alpha[0] = exp(-5.0 * ts / settle_left);
    alpha[1] = exp(-5.0 * ts / settle_right);
    beta[0] = ke * ke_left * (1.0 - alpha[0]);
    beta[1] = ke * ke_right * (1.0 - alpha[1]);
    c_theta = ts * wheel_radius / wheel_distance;
    c_v = ts * wheel_radius / 2.0;

    memset(A_aff, 0, sizeof A_aff);
    memset(B_aff, 0, sizeof B_aff);
    for (int i = 0; i < NX; i++) A_aff[0][i][i] = 1.0;
    A_aff[0][0][0] = alpha[0];
    A_aff[0][1][1] = alpha[1];
    A_aff[0][2][0] = c_theta * alpha[0];
    A_aff[0][2][1] = -c_theta * alpha[1];
    B_aff[0][0][0] = beta[0];
    B_aff[0][1][1] = beta[1];
    B_aff[0][2][0] = c_theta * beta[0];
    B_aff[0][2][1] = -c_theta * beta[1];

    /* A row multiplied by the speed v(k+1) = c_v (a w + b u). */
    int row1 = embedding == 0 ? 4 : 3;   /* sinc: Y; sin_cos: X */
    A_aff[1][row1][0] = c_v * alpha[0];
    A_aff[1][row1][1] = c_v * alpha[1];
    B_aff[1][row1][0] = c_v * beta[0];
    B_aff[1][row1][1] = c_v * beta[1];
    if (embedding == 0) {
        A_aff[2][3][2] = ts;             /* X: v(k+1) sinc(theta) theta */
        double v_max = 1.2 * 100.0 * ke * wheel_radius;
        rho_min[0] = -1.0; rho_max[0] = 1.0;
        rho_min[1] = -v_max; rho_max[1] = v_max;
    } else {
        A_aff[2][4][0] = c_v * alpha[0]; /* Y: cos(theta) v(k+1) */
        A_aff[2][4][1] = c_v * alpha[1];
        B_aff[2][4][0] = c_v * beta[0];
        B_aff[2][4][1] = c_v * beta[1];
        rho_min[0] = rho_min[1] = -1.0;
        rho_max[0] = rho_max[1] = 1.0;
    }
}

/* A(rho), B(rho) */
static void matrices(const double *rho, double a[NX][NX], double b[NX][NU]) {
    for (int i = 0; i < NX; i++) {
        for (int j = 0; j < NX; j++)
            a[i][j] = A_aff[0][i][j] + rho[0] * A_aff[1][i][j] + rho[1] * A_aff[2][i][j];
        for (int j = 0; j < NU; j++)
            b[i][j] = B_aff[0][i][j] + rho[0] * B_aff[1][i][j] + rho[1] * B_aff[2][i][j];
    }
}

/* rho = f(z), z = [x(j), x(j+1)] */
static void f_rho(const double *z, double *rho) {
    double theta = z[2];
    if (embedding == 1) {
        rho[0] = sin(theta);
        rho[1] = cos(theta);
    } else {
        double v_next = wheel_radius / 2.0 * (z[NX] + z[NX + 1]);
        rho[0] = cos(theta);
        rho[1] = v_next * sinc(theta);
    }
}

/* d f_rho / dz */
static void jacobian_rho(const double *z, double jac[2][2 * NX]) {
    double theta = z[2];
    memset(jac, 0, sizeof(double) * 2 * 2 * NX);
    if (embedding == 1) {
        jac[0][2] = cos(theta);
        jac[1][2] = -sin(theta);
    } else {
        double v_next = wheel_radius / 2.0 * (z[NX] + z[NX + 1]);
        jac[0][2] = -sin(theta);
        jac[1][2] = v_next * dsinc(theta);
        jac[1][NX] = wheel_radius / 2.0 * sinc(theta);
        jac[1][NX + 1] = wheel_radius / 2.0 * sinc(theta);
    }
}

static void saturate_rho(double *rho) {
    for (int i = 0; i < 2; i++) rho[i] = tl_sat(rho[i], rho_min[i], rho_max[i]);
}

static void pair(double s[][NX], int j, double *z) {
    for (int i = 0; i < NX; i++) {
        z[i] = s[j][i];
        z[NX + i] = s[j + 1][i];
    }
}

/* ------------------------------------------------------------------ */
/* Scheduling                                                          */

/* (x(k), x*(k+1|k-1), ..., x*(k+N-1|k-1), x*(k+N-1|k-1)) */
static void previous_sequence(const double *x0, double s_prev[][NX], int n, double seq[][NX]) {
    memcpy(seq[0], x0, sizeof(double) * NX);
    for (int j = 1; j < n; j++) memcpy(seq[j], s_prev[j + 1], sizeof(double) * NX);
    memcpy(seq[n], s_prev[n], sizeof(double) * NX);
}

/* P (n x 2) from strategy `kind`; s_prev is NULL on the first sample. */
static void estimate(int kind, const double *x0, double s_prev[][NX], int n, double p[][2]) {
    static double seq[NMAX + 1][NX];
    double z[2 * NX], rho[2];
    if (kind == 3) kind = initial;            /* iterative: its first estimate */
    if (kind == 0 || s_prev == NULL) {         /* frozen */
        for (int i = 0; i < NX; i++) z[i] = z[NX + i] = x0[i];
        f_rho(z, rho);
        for (int j = 0; j < n; j++) { p[j][0] = rho[0]; p[j][1] = rho[1]; }
        return;
    }
    previous_sequence(x0, s_prev, n, seq);
    if (kind == 1) {                           /* warm start */
        for (int j = 0; j < n; j++) {
            pair(seq, j, z);
            f_rho(z, p[j]);
        }
        return;
    }
    /* taylor: rho(j) = sat(rho(j-1) + sigma (z(j) - z(j-1))) */
    double z_prev[2 * NX], sigma[2][2 * NX];
    pair(seq, 0, z_prev);
    jacobian_rho(z_prev, sigma);
    f_rho(z_prev, rho);
    p[0][0] = rho[0];
    p[0][1] = rho[1];
    for (int j = 1; j < n; j++) {
        pair(seq, j, z);
        for (int r = 0; r < 2; r++) {
            double inc = 0;
            for (int c = 0; c < 2 * NX; c++) inc += sigma[r][c] * (z[c] - z_prev[c]);
            rho[r] += inc;
        }
        saturate_rho(rho);
        p[j][0] = rho[0];
        p[j][1] = rho[1];
        memcpy(z_prev, z, sizeof z);
    }
}

/* ------------------------------------------------------------------ */
/* MPC                                                                 */

static double S[NMAX + 1][NX];   /* predicted states x(k), ..., x(k+N) */
static double P[NMAX][2];        /* scheduling used */
static double U[NUMAX];          /* the plan */
static int has_solution, n_last;
static double u_prev[NU];

/* Lifted prediction X = free + Gamma U for the scheduling P. */
static void predict(double p[][2], const double *x0, const double *d, int n,
                    double free_[][NX], double *gamma /* (n NX) x (n NU) */) {
    static double g[NX][NUMAX], tmp[NX][NUMAX];
    double x[NX], a[NX][NX], b[NX][NU];
    int n_u = n * NU;
    memcpy(x, x0, sizeof x);
    memset(g, 0, sizeof g);
    for (int j = 0; j < n; j++) {
        matrices(p[j], a, b);
        /* x(j+1) = A_j x(j) + B_j d,  G_{j+1} = [A_j G_j, B_j] */
        double next[NX];
        for (int i = 0; i < NX; i++) {
            double ax = 0, bd = 0;
            for (int k = 0; k < NX; k++) ax += a[i][k] * x[k];
            for (int k = 0; k < NU; k++) bd += b[i][k] * d[k];
            next[i] = ax + bd;
        }
        memcpy(x, next, sizeof x);
        int cols = j * NU;
        for (int i = 0; i < NX; i++)
            for (int c = 0; c < cols; c++) {
                double sum = 0;
                for (int k = 0; k < NX; k++) sum += a[i][k] * g[k][c];
                tmp[i][c] = sum;
            }
        for (int i = 0; i < NX; i++) {
            for (int c = 0; c < cols; c++) g[i][c] = tmp[i][c];
            for (int c = 0; c < NU; c++) g[i][cols + c] = b[i][c];
        }
        memcpy(free_[j], x, sizeof x);
        for (int i = 0; i < NX; i++)
            memcpy(&gamma[(j * NX + i) * n_u], g[i], sizeof(double) * n_u);
    }
}

/* One QP for the scheduling p: the plan into u, the states into s. */
static int solve_qp(double p[][2], const double *x0, double ref[][3], const double *d,
                    const double *u0, double w[][3][3], const double *profile, int n,
                    double *u, double s[][NX]) {
    static double free_[NMAX][NX], gamma[NMAX * NX * NUMAX];
    static double gy[NMAX * NY * NUMAX], wg[NMAX * NY * NUMAX];
    static double h[NUMAX * NUMAX], g[NUMAX], err[NMAX * NY], lb[NUMAX], ub[NUMAX];
    int n_u = n * NU;
    predict(p, x0, d, n, free_, gamma);

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < NY; i++) {
            memcpy(&gy[(j * NY + i) * n_u], &gamma[(j * NX + 2 + i) * n_u], sizeof(double) * n_u);
            err[j * NY + i] = free_[j][2 + i] - ref[j][i];
        }
        double pw[3][3];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) pw[r][c] = profile[j] * w[j][r][c];
        for (int r = 0; r < NY; r++)
            for (int c = 0; c < n_u; c++) {
                double sum = 0;
                for (int k = 0; k < NY; k++) sum += pw[r][k] * gy[(j * NY + k) * n_u + c];
                wg[(j * NY + r) * n_u + c] = sum;
            }
    }
    /* H = Gy' W Gy + R + D' Rd D,  g = (W Gy)' err - D' Rd e0 */
    tl_mat_mul_tn(gy, wg, h, n_u, n * NY, n_u);
    tl_mat_mul_tn(wg, err, g, n_u, n * NY, 1);
    for (int i = 0; i < n_u; i++) {
        h[i * n_u + i] += r_input + rd_input + (i + NU < n_u ? rd_input : 0.0);
        if (i + NU < n_u) {
            h[i * n_u + i + NU] -= rd_input;
            h[(i + NU) * n_u + i] -= rd_input;
        }
        lb[i] = u_min;
        ub[i] = u_max;
    }
    for (int i = 0; i < NU; i++) g[i] -= rd_input * u_prev[i];

    int iters = tl_qp_box(h, g, lb, ub, u0, u, n_u);

    /* The states the plan gives: free + Gamma U. */
    static double forced[NMAX * NX];
    tl_mat_mul(gamma, u, forced, n * NX, n_u, 1);
    memcpy(s[0], x0, sizeof(double) * NX);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < NX; i++) s[j + 1][i] = free_[j][i] + forced[j * NX + i];
    return iters;
}

/* Rewrites states given in the frame of k-1 in the frame of k. */
static void realign(double s[][NX], int rows, const double *pose) {
    double c = cos(pose[0]), sn = sin(pose[0]);
    for (int j = 0; j < rows; j++) {
        double dx = s[j][3] - pose[1];
        double dy = s[j][4] - pose[2];
        s[j][2] -= pose[0];
        s[j][3] = c * dx - sn * dy;
        s[j][4] = sn * dx + c * dy;
    }
}

static void mpc_step(const double *x0, double ref[][3], const double *d, const double *frame,
                     double w[][3][3], int n, double *u_out) {
    static double s_prev[NMAX + 1][NX], s[NMAX + 1][NX], p_next[NMAX][2];
    static double u0[NUMAX], u[NUMAX];
    double profile[NMAX];
    int n_u = n * NU;
    for (int j = 0; j < n; j++)
        profile[j] = (n < 2 || weight_growth == 1.0) ? 1.0
                                                     : pow(weight_growth, (double)j / (double)(n - 1));
    profile[n - 1] = 1.0;   /* the terminal weight is not scaled */

    if (has_solution) {
        memcpy(s_prev, S, sizeof(double) * (n + 1) * NX);
        realign(s_prev, n + 1, frame);
        /* warm start: the previous plan shifted by one step */
        memcpy(u0, &U[NU], sizeof(double) * (n - 1) * NU);
        memcpy(&u0[(n - 1) * NU], &U[(n - 1) * NU], sizeof(double) * NU);
    }
    estimate(scheduling, x0, has_solution ? s_prev : NULL, n, P);

    int max_qp = scheduling == 3 ? iterations : 1;
    qps = 0;
    qp_iterations = 0;
    while (1) {
        qp_iterations += solve_qp(P, x0, ref, d, (has_solution || qps > 0) ? u0 : NULL, w, profile,
                                  n, u, s);
        qps += 1;
        if (qps >= max_qp) break;
        /* iterative: P = sat(P + gamma (f_rho(prediction) - P)) until settled */
        double step_max = 0, z[2 * NX];
        for (int j = 0; j < n; j++) {
            double rho[2];
            pair(s, j, z);
            f_rho(z, rho);
            for (int i = 0; i < 2; i++) {
                double e = rho[i] - P[j][i];
                if (fabs(e) > step_max) step_max = fabs(e);
                p_next[j][i] = P[j][i] + relaxation * e;
            }
            saturate_rho(p_next[j]);
        }
        if (step_max < eps) break;
        memcpy(P, p_next, sizeof(double) * n * 2);
        memcpy(u0, u, sizeof(double) * n_u);
    }

    /* Mismatch between the scheduling used and the one of the prediction. */
    rho_error = 0;
    for (int j = 0; j < n; j++) {
        double rho[2], z[2 * NX];
        pair(s, j, z);
        f_rho(z, rho);
        for (int i = 0; i < 2; i++)
            if (fabs(rho[i] - P[j][i]) > rho_error) rho_error = fabs(rho[i] - P[j][i]);
    }
    memcpy(S, s, sizeof(double) * (n + 1) * NX);
    memcpy(U, u, sizeof(double) * n_u);
    has_solution = 1;
    u_prev[0] = u_out[0] = U[0];
    u_prev[1] = u_out[1] = U[1];
}

/* ------------------------------------------------------------------ */
/* Controller                                                          */

static double u_now[NU], w_prev[NU], dist[NU];
static double ref[NMAX][3], weights[NMAX][3][3];

static void gradient(const double *f, double *df, int n) {
    df[0] = f[1] - f[0];
    df[n - 1] = f[n - 1] - f[n - 2];
    for (int i = 1; i < n - 1; i++) df[i] = (f[i + 1] - f[i - 1]) / 2.0;
}

void ctl_reset(void) {
    has_solution = 0;
    n_last = 0;
    u_prev[0] = u_prev[1] = 0;
    u_now[0] = u_now[1] = 0;
    w_prev[0] = w_prev[1] = 0;
    dist[0] = dist[1] = 0;
}

void ctl_step(void) {
    int n = tl_preview_count();
    if (n < 2) return;
    if (n != n_last) {           /* a new horizon: no previous solution fits */
        has_solution = 0;
        n_last = n;
    }
    build_model();

    /* Input disturbance from the last wheel step, low-pass filtered. */
    double w[NU] = {w_l, w_r};
    for (int i = 0; i < NU; i++) {
        double d_now = (w[i] - alpha[i] * w_prev[i]) / beta[i] - u_now[i];
        dist[i] += disturbance_filter * (d_now - dist[i]);
        w_prev[i] = w[i];
    }

    /* Reference [theta, X, Y]: theta is the heading of the track tangent. */
    double dx[NMAX], dy[NMAX];
    gradient(px, dx, n);
    gradient(py, dy, n);
    for (int j = 0; j < n; j++) {
        ref[j][0] = atan2(dx[j], dy[j]);
        ref[j][1] = px[j];
        ref[j][2] = py[j];
    }

    /* Stage weights: diag(Q), or Q along the track (contouring); Q_N last. */
    for (int j = 0; j < n; j++) {
        int last = j == n - 1;
        double q0 = last ? qn_theta : q_theta;
        double q1 = last ? qn_lateral : q_lateral;
        double q2 = last ? qn_longitudinal : q_longitudinal;
        memset(weights[j], 0, sizeof weights[j]);
        weights[j][0][0] = q0;
        if (contouring) {
            double s = sin(ref[j][0]), c = cos(ref[j][0]);
            double nrm[2] = {c, -s}, tan_[2] = {s, c};
            for (int r = 0; r < 2; r++)
                for (int k = 0; k < 2; k++)
                    weights[j][1 + r][1 + k] = (q1 * nrm[r]) * nrm[k] + (q2 * tan_[r]) * tan_[k];
        } else {
            weights[j][1][1] = q1;
            weights[j][2][2] = q2;
        }
    }

    /* The pose of now in the frame of the previous step (odometry). */
    double frame[3] = {c_theta * (w[0] - w[1]), 0.0, c_v * (w[0] + w[1])};
    double x0[NX] = {w[0], w[1], 0, 0, 0};
    mpc_step(x0, ref, dist, frame, weights, n, u_now);
    u_l = u_now[0];
    u_r = u_now[1];

    /* What the scene draws: the predicted path. */
    double th[NMAX], x[NMAX], y[NMAX];
    for (int j = 0; j < n; j++) {
        th[j] = S[j + 1][2];
        x[j] = S[j + 1][3];
        y[j] = S[j + 1][4];
    }
    tl_predicted(x, y, th, n);
    tl_set_iterations(qps);
    theta_ref = ref[0][0];
    theta_pred = th[0];
    w_l_filt = w_l;
    w_r_filt = w_r;
}
