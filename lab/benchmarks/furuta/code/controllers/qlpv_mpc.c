#include <tracelab.h>

/* qLPV NMPC of the Furuta pendulum: swing-up from hanging down, balance
 * upright and track the arm reference. The scheme of "Fast qLPV NMPC using
 * CHRONOS: Parameter Trajectory Estimation and Application to the Furuta
 * Pendulum" (Tristao, Sanches, Medero, Morato), ported from
 * simulacao_furuta/my_furuta_mpc_step.m with the CHRONOS QP replaced by
 * TraceLab's box QP (tl_qp_box).
 *
 * MODEL. The Euler discretization (Ts = the plant's sample period) of the
 * pendulum's Euler-Lagrange equations, embedded exactly as a qLPV model with
 * eight scheduling parameters rho = f_rho(x), x = [th1, th2, w1, w2]:
 *
 *     x(k+1) = A(rho) x(k) + B(rho) u(k)
 *     A = [1  0     Ts      0    ]      B = [0   ]
 *         [0  1     0       Ts   ]          [0   ]
 *         [0  rho1  1+rho2  rho3 ]          [rho7]
 *         [0  rho4  rho5    1+rho6]         [rho8]
 *
 * sin(th2) = sinc(th2) th2 keeps gravity linear in th2 with no singularity
 * upright; the rates are saturated at +-w_max before f_rho, so rho stays in
 * its scheduling set even along a diverging prediction.
 *
 * SCHEDULING TRAJECTORY. The QP needs rho(k+j|k), j = 0..N-1, which depends
 * on the states it is about to choose. Estimates (the paper's Section 3):
 *     frozen      rho(k) over the whole horizon
 *     warm_start  f_rho along the previous solution, x*(k+j|k-1)
 *     taylor      rho(k+j|k-1) + sigma_k (x(k) - x*(k+j|k-1)), saturated,
 *                 sigma_k = d f_rho / dx at x(k) (eq. 13)
 *     iterative   solve, recompute rho from the new prediction, solve
 *                 again... until |dP|_inf < eps or `iterations` QPs
 *
 * MPC. For a given trajectory the prediction is linear in U = [u(k) ...
 * u(k+N-1)] and
 *     min  sum_{j=1..N} |x(k+j) - r(k+j)|^2_Q + rd sum_j (u(k+j) - u(k+j-1))^2
 *     s.t. u_min <= u <= u_max
 * is a box QP, solved by projected Newton (below) or tl_qp_box. Q's last
 * stage is scaled by qn_scale. The arm reference steps
 * by `step` at `step_time` (the paper: 0 -> pi at 11 s); th2 is measured
 * modulo 2 pi, so the pendulum goes up by the nearest side.
 */

TL_PARAM_ENUM(scheduling, 1, "frozen|warm_start|taylor|iterative");
TL_PARAM_INT(iterations, 5, 1, 50);          /* iterative: most QPs (l_max) */
TL_PARAM(eps, 1e-3, "", 0, 1);               /* iterative: P settled */
TL_PARAM_INT(horizon, 40, 2, 64);            /* N */
TL_PARAM(q_theta1, 10, "", 0, 1e9);
TL_PARAM(q_theta2, 100, "", 0, 1e9);
TL_PARAM(q_dtheta1, 5, "", 0, 1e9);
TL_PARAM(q_dtheta2, 1, "", 0, 1e9);
TL_PARAM(qn_scale, 1, "", 0, 1e6);           /* last stage: qn_scale Q */
TL_PARAM(rd, 100, "", 0, 1e9);               /* |du|^2 */
TL_PARAM(r_input, 0, "", 0, 1e9);            /* |u|^2 */
TL_PARAM(u_min, -0.6, "N m", -100, 0);
TL_PARAM(u_max, 0.6, "N m", 0, 100);
TL_PARAM(w_max, 30, "rad/s", 1, 1000);       /* rates saturated in f_rho */
TL_PARAM_ENUM(qp_solver, 0, "projected_newton|tl_qp_box");

/* The reference: th1 = theta1_ref (+ step after step_time), th2 = 0. */
TL_PARAM(theta1_ref, 0, "rad", -100, 100);
TL_PARAM(step, M_PI, "rad", -100, 100);
TL_PARAM(step_time, 11, "s", 0, 1e6);        /* 0: no step */

/* The pendulum the model is built from (the plant's defaults). */
TL_PARAM(m2, 0.09, "kg", 0.001, 10);
TL_PARAM(L1, 0.085, "m", 0.01, 2);
TL_PARAM(l2, 0.13, "m", 0.01, 2);
TL_PARAM(I1, 0.005, "kg m^2", 1e-6, 1);
TL_PARAM(I2, 5.07e-4, "kg m^2", 1e-7, 1);
TL_PARAM(mu1, 0.001, "N m s/rad", 0, 1);
TL_PARAM(mu2, 0.001, "N m s/rad", 0, 1);
TL_PARAM(g, 9.81, "m/s^2", 0, 100);

#define NX 4
#define NR 8                   /* scheduling parameters */
#define NMAX 64

TL_IN(th1);                    /* arm angle, rad */
TL_IN(th2);                    /* pendulum angle, rad, 0 upright */
TL_IN(w1);                     /* rates, rad/s */
TL_IN(w2);
TL_OUT_PLAN(u, NMAX);          /* torque, N m: the whole plan */

TL_DIAG(theta1_r, "rad");      /* arm reference now */
TL_DIAG(theta2_w, "rad");      /* pendulum angle the controller uses (wrapped) */
TL_DIAG(cost, "");             /* the QP's cost at its solution */
TL_DIAG(qps, "");              /* QPs solved this step */
TL_DIAG(qp_iterations, "");    /* active-set iterations, all QPs */
TL_DIAG(rho_error, "");        /* max |f_rho(prediction) - rho used| */
TL_DIAG(rho_norm, "");         /* |P_k|_2, the trajectory used */

/* ------------------------------------------------------------------ */
/* Model                                                               */

static double alpha_, beta_, gamma_, delta_, eta_;

static void build_model(void) {
    alpha_ = I1 + m2 * L1 * L1;
    beta_ = m2 * l2 * l2;
    gamma_ = m2 * L1 * l2;
    delta_ = I2 + m2 * l2 * l2;
    eta_ = m2 * g * l2;
}

static void sinc_d(double t, double *s, double *ds) {
    if (fabs(t) < 1e-4) {
        double t2 = t * t;
        *s = 1 - t2 / 6 + t2 * t2 / 120;
        *ds = -t / 3 + t2 * t / 30;
    } else {
        *s = sin(t) / t;
        *ds = (t * cos(t) - sin(t)) / (t * t);
    }
}

/* rho = f_rho(x) */
static void f_rho(const double *x, double *rho) {
    double ts = tl_ts();
    double th = x[1];
    double v1 = tl_sat(x[2], -w_max, w_max), v2 = tl_sat(x[3], -w_max, w_max);
    double s = sin(th), c = cos(th), s2 = sin(2 * th), sc, dsc;
    sinc_d(th, &sc, &dsc);
    double a = alpha_ + beta_ * s * s;
    double det = a * delta_ - gamma_ * gamma_ * c * c;
    double k = ts / det;
    rho[0] = k * (-gamma_ * eta_ * c * sc);
    rho[1] = k * (-delta_ * beta_ * s2 * v2 - delta_ * mu1 - gamma_ * beta_ * s * c * c * v1);
    rho[2] = k * (delta_ * gamma_ * s * v2 + gamma_ * c * mu2);
    rho[3] = k * (a * eta_ * sc);
    rho[4] = k * (gamma_ * beta_ * c * s2 * v2 + gamma_ * c * mu1 + a * beta_ * s * c * v1);
    rho[5] = k * (-gamma_ * gamma_ * c * s * v2 - a * mu2);
    rho[6] = k * delta_;
    rho[7] = k * (-gamma_ * c);
}

/* sigma = d f_rho / dx (NR x NX), by the quotient rule as in the .m */
static void jacobian_rho(const double *x, double sigma[NR][NX]) {
    double ts = tl_ts();
    double th = x[1];
    int in1 = fabs(x[2]) < w_max, in2 = fabs(x[3]) < w_max; /* outside: saturated, flat */
    double v1 = tl_sat(x[2], -w_max, w_max), v2 = tl_sat(x[3], -w_max, w_max);
    double s = sin(th), c = cos(th), s2 = sin(2 * th), c2 = cos(2 * th), sc, dsc;
    sinc_d(th, &sc, &dsc);
    double a = alpha_ + beta_ * s * s, da = 2 * beta_ * s * c;
    double det = a * delta_ - gamma_ * gamma_ * c * c;
    double ddet = delta_ * da + 2 * gamma_ * gamma_ * s * c;
    double n[8], dn[8], dn1[8], dn2[8]; /* numerators, d/dth2, d/dw1, d/dw2 */
    memset(dn1, 0, sizeof dn1);
    memset(dn2, 0, sizeof dn2);
    n[0] = -gamma_ * eta_ * c * sc;
    dn[0] = gamma_ * eta_ * s * sc - gamma_ * eta_ * c * dsc;
    n[1] = -delta_ * beta_ * s2 * v2 - delta_ * mu1 - gamma_ * beta_ * s * c * c * v1;
    dn[1] = -2 * delta_ * beta_ * c2 * v2 - gamma_ * beta_ * c * (c * c - 2 * s * s) * v1;
    dn1[1] = -gamma_ * beta_ * s * c * c;
    dn2[1] = -delta_ * beta_ * s2;
    n[2] = delta_ * gamma_ * s * v2 + gamma_ * c * mu2;
    dn[2] = delta_ * gamma_ * c * v2 - gamma_ * s * mu2;
    dn2[2] = delta_ * gamma_ * s;
    n[3] = a * eta_ * sc;
    dn[3] = eta_ * (da * sc + a * dsc);
    n[4] = gamma_ * beta_ * c * s2 * v2 + gamma_ * c * mu1 + a * beta_ * s * c * v1;
    dn[4] = gamma_ * beta_ * (-s * s2 + 2 * c * c2) * v2 - gamma_ * mu1 * s +
            beta_ * (da * s * c + a * c2) * v1;
    dn1[4] = a * beta_ * s * c;
    dn2[4] = gamma_ * beta_ * c * s2;
    n[5] = -gamma_ * gamma_ * c * s * v2 - a * mu2;
    dn[5] = -gamma_ * gamma_ * c2 * v2 - da * mu2;
    dn2[5] = -gamma_ * gamma_ * c * s;
    n[6] = delta_;
    dn[6] = 0;
    n[7] = -gamma_ * c;
    dn[7] = gamma_ * s;
    for (int i = 0; i < NR; i++) {
        sigma[i][0] = 0;
        sigma[i][1] = ts * (dn[i] * det - n[i] * ddet) / (det * det);
        sigma[i][2] = in1 ? ts * dn1[i] / det : 0;
        sigma[i][3] = in2 ? ts * dn2[i] / det : 0;
    }
}

/* The bounds of the scheduling set: f_rho over th2 in [-pi, pi] and the
   rates in [-w_max, w_max] (a grid, once per model). */
static double rho_lo[NR], rho_hi[NR];

static void scheduling_set(void) {
    for (int i = 0; i < NR; i++) {
        rho_lo[i] = 1e300;
        rho_hi[i] = -1e300;
    }
    for (int a = 0; a <= 72; a++)
        for (int b = 0; b <= 4; b++)
            for (int c = 0; c <= 4; c++) {
                double x[NX] = {0, -M_PI + a * M_PI / 36, -w_max + b * w_max / 2,
                                -w_max + c * w_max / 2};
                double rho[NR];
                f_rho(x, rho);
                for (int i = 0; i < NR; i++) {
                    if (rho[i] < rho_lo[i]) rho_lo[i] = rho[i];
                    if (rho[i] > rho_hi[i]) rho_hi[i] = rho[i];
                }
            }
}

/* A(rho), B(rho) */
static void matrices(const double *rho, double a[NX][NX], double *b) {
    double ts = tl_ts();
    memset(a, 0, sizeof(double) * NX * NX);
    for (int i = 0; i < NX; i++) a[i][i] = 1.0;
    a[0][2] = ts;
    a[1][3] = ts;
    a[2][1] = rho[0];
    a[2][2] += rho[1];
    a[2][3] = rho[2];
    a[3][1] = rho[3];
    a[3][2] = rho[4];
    a[3][3] += rho[5];
    b[0] = b[1] = 0;
    b[2] = rho[6];
    b[3] = rho[7];
}

/* ------------------------------------------------------------------ */
/* Scheduling                                                          */

static double S[NMAX + 1][NX];   /* predicted states x(k), ..., x(k+N) */
static double P[NMAX][NR];       /* scheduling used, rho(k), ..., rho(k+N-1) */
static double U[NMAX];           /* the plan */
static int has_solution, n_last;
static double u_prev;

/* P (n x NR); s_prev / p_prev are the previous step's states and
   scheduling (frame of now), NULL on the first sample. */
static void estimate(int kind, const double *x0, double s_prev[][NX], double p_prev[][NR], int n,
                     double p[][NR]) {
    f_rho(x0, p[0]);
    if (kind == 3) kind = 1;                   /* iterative: starts from the warm start */
    if (kind == 0 || s_prev == NULL) {         /* frozen */
        for (int j = 1; j < n; j++) memcpy(p[j], p[0], sizeof(double) * NR);
        return;
    }
    if (kind == 1) {                           /* warm start: f_rho(x*(k+j|k-1)) */
        for (int j = 1; j < n; j++) f_rho(s_prev[j + 1], p[j]);
        return;
    }
    /* taylor: sat(rho(k+j|k-1) + sigma_k (x(k) - x*(k+j|k-1))) */
    double sigma[NR][NX];
    jacobian_rho(x0, sigma);
    for (int j = 1; j < n; j++) {
        const double *rho_prev = p_prev[j + 1 < n ? j + 1 : n - 1];
        for (int i = 0; i < NR; i++) {
            double inc = 0;
            for (int c = 0; c < NX; c++) inc += sigma[i][c] * (x0[c] - s_prev[j + 1][c]);
            p[j][i] = tl_sat(rho_prev[i] + inc, rho_lo[i], rho_hi[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Box QP                                                              */

static double quad(const double *h, const double *g, const double *x, int n, double *grad) {
    double f = 0;
    for (int i = 0; i < n; i++) {
        double hx = 0;
        for (int k = 0; k < n; k++) hx += h[i * n + k] * x[k];
        if (grad) grad[i] = hx + g[i];
        f += x[i] * (0.5 * hx + g[i]);
    }
    return f;
}

/* min 1/2 x'Hx + g'x, lb <= x <= ub (H positive definite) by Bertsekas'
   projected Newton: a Newton step on the variables off their bounds, a
   scaled gradient step on the ones a bound holds, a projected Armijo line
   search. Monotone, so it never cycles; on a QP it lands on the solution
   once the active set is right. Returns the iterations. */
static int qp_projected_newton(const double *h, const double *g, const double *lb,
                               const double *ub, const double *x0, double *x, int n) {
    static double grad[NMAX], d[NMAX], xn[NMAX], hff[NMAX * NMAX], rhs[NMAX];
    int free_[NMAX];
    for (int i = 0; i < n; i++)
        x[i] = tl_sat(x0 ? x0[i] : -g[i] / h[i * n + i], lb[i], ub[i]);
    double f = quad(h, g, x, n, grad);
    int it;
    for (it = 1; it <= 100; it++) {
        double pg = 0;
        for (int i = 0; i < n; i++) {
            double step = fabs(tl_sat(x[i] - grad[i], lb[i], ub[i]) - x[i]);
            if (step > pg) pg = step;
        }
        if (pg < 1e-9 * (1 + fabs(f))) break;
        double band = fmin(pg, 1e-6);
        int nf = 0;
        for (int i = 0; i < n; i++) {
            int held = (x[i] <= lb[i] + band && grad[i] > 0) || (x[i] >= ub[i] - band && grad[i] < 0);
            if (held) d[i] = -grad[i] / h[i * n + i];
            else free_[nf++] = i;
        }
        for (int a = 0; a < nf; a++) {
            rhs[a] = -grad[free_[a]];
            for (int b = 0; b < nf; b++) hff[a * nf + b] = h[free_[a] * n + free_[b]];
        }
        if (nf > 0 && tl_mat_solve(hff, rhs, nf, 1) != 0)
            for (int a = 0; a < nf; a++) rhs[a] = -grad[free_[a]] / h[free_[a] * n + free_[a]];
        for (int a = 0; a < nf; a++) d[free_[a]] = rhs[a];
        /* Armijo along the projected arc */
        double alpha = 1, fn = f;
        int k;
        for (k = 0; k < 40; k++) {
            double slope = 0;
            for (int i = 0; i < n; i++) {
                xn[i] = tl_sat(x[i] + alpha * d[i], lb[i], ub[i]);
                slope += grad[i] * (xn[i] - x[i]);
            }
            fn = quad(h, g, xn, n, NULL);
            if (fn <= f + 1e-4 * slope) break;
            alpha *= 0.5;
        }
        if (k == 40 || fn >= f) break;   /* no progress left: at the solution */
        memcpy(x, xn, sizeof(double) * n);
        f = quad(h, g, x, n, grad);
    }
    return it;
}

/* ------------------------------------------------------------------ */
/* MPC                                                                 */

/* Lifted prediction x(k+j+1) = free_j + Gamma_j U for the scheduling p. */
static void predict(double p[][NR], const double *x0, int n, double free_[][NX],
                    double *gamma /* (n NX) x n */) {
    static double gm[NX][NMAX], tmp[NX][NMAX];
    double x[NX], a[NX][NX], b[NX];
    memcpy(x, x0, sizeof x);
    memset(gm, 0, sizeof gm);
    for (int j = 0; j < n; j++) {
        matrices(p[j], a, b);
        double next[NX];
        for (int i = 0; i < NX; i++) {
            double ax = 0;
            for (int k = 0; k < NX; k++) ax += a[i][k] * x[k];
            next[i] = ax;
        }
        memcpy(x, next, sizeof x);
        /* G_{j+1} = [A_j G_j, B_j] */
        for (int i = 0; i < NX; i++)
            for (int c = 0; c < j; c++) {
                double sum = 0;
                for (int k = 0; k < NX; k++) sum += a[i][k] * gm[k][c];
                tmp[i][c] = sum;
            }
        for (int i = 0; i < NX; i++) {
            for (int c = 0; c < j; c++) gm[i][c] = tmp[i][c];
            gm[i][j] = b[i];
        }
        memcpy(free_[j], x, sizeof x);
        for (int i = 0; i < NX; i++) memcpy(&gamma[(j * NX + i) * n], gm[i], sizeof(double) * n);
    }
}

/* One QP for the scheduling p: the plan into u, the states into s; returns
   the active-set iterations. */
static int solve_qp(double p[][NR], const double *x0, double ref[][NX], const double *u0, int n,
                    double *u, double s[][NX]) {
    static double free_[NMAX][NX], gamma[NMAX * NX * NMAX], wg[NMAX * NX * NMAX];
    static double h[NMAX * NMAX], g[NMAX], err[NMAX * NX], lb[NMAX], ub[NMAX];
    double q[NX] = {q_theta1, q_theta2, q_dtheta1, q_dtheta2};
    predict(p, x0, n, free_, gamma);

    for (int j = 0; j < n; j++) {
        double scale = j == n - 1 ? qn_scale : 1.0;
        for (int i = 0; i < NX; i++) {
            int row = j * NX + i;
            double w = scale * q[i];
            err[row] = free_[j][i] - ref[j][i];
            for (int c = 0; c < n; c++) wg[row * n + c] = w * gamma[row * n + c];
        }
    }
    /* H = Gamma' Q Gamma + r I + rd D'D,  g = (Q Gamma)' err - rd e0 u_prev */
    tl_mat_mul_tn(gamma, wg, h, n, n * NX, n);
    tl_mat_mul_tn(wg, err, g, n, n * NX, 1);
    for (int i = 0; i < n; i++) {
        h[i * n + i] += r_input + rd + (i + 1 < n ? rd : 0.0);
        if (i + 1 < n) {
            h[i * n + i + 1] -= rd;
            h[(i + 1) * n + i] -= rd;
        }
        lb[i] = u_min;
        ub[i] = u_max;
    }
    g[0] -= rd * u_prev;

    int iters = qp_solver == 0 ? qp_projected_newton(h, g, lb, ub, u0, u, n)
                               : tl_qp_box(h, g, lb, ub, u0, u, n);

    /* The states the plan gives: free + Gamma U; and the cost. */
    static double forced[NMAX * NX];
    tl_mat_mul(gamma, u, forced, n * NX, n, 1);
    memcpy(s[0], x0, sizeof(double) * NX);
    double j_cost = 0;
    for (int j = 0; j < n; j++) {
        double scale = j == n - 1 ? qn_scale : 1.0;
        for (int i = 0; i < NX; i++) {
            s[j + 1][i] = free_[j][i] + forced[j * NX + i];
            double e = s[j + 1][i] - ref[j][i];
            j_cost += scale * q[i] * e * e;
        }
        double du = u[j] - (j == 0 ? u_prev : u[j - 1]);
        j_cost += rd * du * du + r_input * u[j] * u[j];
    }
    cost = j_cost;
    return iters;
}

static void mpc_step(const double *x0, double ref[][NX], int n) {
    static double s_prev[NMAX + 1][NX], p_prev[NMAX][NR], s[NMAX + 1][NX], p_next[NMAX][NR];
    static double u0[NMAX], plan[NMAX];

    if (has_solution) {
        memcpy(s_prev, S, sizeof(double) * (n + 1) * NX);
        memcpy(p_prev, P, sizeof(double) * n * NR);
        /* warm start: the previous plan shifted by one step */
        memcpy(u0, &U[1], sizeof(double) * (n - 1));
        u0[n - 1] = U[n - 1];
    }
    estimate(scheduling, x0, has_solution ? s_prev : NULL, has_solution ? p_prev : NULL, n, P);

    int max_qp = scheduling == 3 ? iterations : 1;
    qps = 0;
    qp_iterations = 0;
    while (1) {
        qp_iterations += solve_qp(P, x0, ref, (has_solution || qps > 0) ? u0 : NULL, n, plan, s);
        qps += 1;
        if (qps >= max_qp) break;
        /* iterative: P = f_rho(this prediction) until it settles */
        double step_max = 0;
        memcpy(p_next[0], P[0], sizeof(double) * NR);
        for (int j = 1; j < n; j++) {
            f_rho(s[j], p_next[j]);
            for (int i = 0; i < NR; i++)
                if (fabs(p_next[j][i] - P[j][i]) > step_max) step_max = fabs(p_next[j][i] - P[j][i]);
        }
        memcpy(P, p_next, sizeof(double) * n * NR);
        memcpy(u0, plan, sizeof(double) * n);
        if (step_max < eps) break;
    }

    /* Mismatch between the scheduling used and the one of the prediction. */
    rho_error = 0;
    rho_norm = 0;
    for (int j = 0; j < n; j++) {
        double rho[NR];
        f_rho(s[j], rho);
        for (int i = 0; i < NR; i++) {
            if (fabs(rho[i] - P[j][i]) > rho_error) rho_error = fabs(rho[i] - P[j][i]);
            rho_norm += P[j][i] * P[j][i];
        }
    }
    rho_norm = sqrt(rho_norm);
    memcpy(S, s, sizeof(double) * (n + 1) * NX);
    memcpy(U, plan, sizeof(double) * n);
    has_solution = 1;
    u_prev = U[0];
}

/* ------------------------------------------------------------------ */
/* Controller                                                          */

static long long turns;          /* th2 = theta2_w + 2 pi turns */
static double set_key;           /* the parameters scheduling_set() saw */

void ctl_reset(void) {
    has_solution = 0;
    n_last = 0;
    u_prev = 0;
    turns = 0;
    set_key = 0;
}

void ctl_step(void) {
    static double ref[NMAX][NX];
    int n = horizon;
    if (n != n_last) {           /* a new horizon: no previous solution fits */
        has_solution = 0;
        n_last = n;
    }
    build_model();
    /* The scheduling set follows the model's parameters. */
    double key = tl_ts() + m2 + 3 * L1 + 5 * l2 + 7 * I1 + 11 * I2 + 13 * mu1 + 17 * mu2 + 19 * g +
                 23 * w_max;
    if (key != set_key) {
        scheduling_set();
        set_key = key;
    }

    /* th2 modulo 2 pi; the previous prediction follows the same turn. */
    long long k = (long long)floor(th2 / (2 * M_PI) + 0.5);
    if (has_solution && k != turns)
        for (int j = 0; j <= n; j++) S[j][1] -= 2 * M_PI * (double)(k - turns);
    turns = k;
    double x0[NX] = {th1, th2 - 2 * M_PI * (double)k, w1, w2};

    /* The reference over the horizon. */
    double t = tl_time();
    for (int j = 0; j < n; j++) {
        double tj = t + (j + 1) * tl_ts();
        ref[j][0] = theta1_ref + ((step_time > 0 && tj >= step_time) ? step : 0.0);
        ref[j][1] = ref[j][2] = ref[j][3] = 0;
    }

    mpc_step(x0, ref, n);
    for (int j = 0; j < NMAX; j++) u[j] = U[j < n ? j : n - 1];

    /* The predicted arm and pendulum angles, drawn by the scene. */
    double p1[NMAX], p2[NMAX];
    for (int j = 0; j < n; j++) {
        p1[j] = S[j + 1][0];
        p2[j] = S[j + 1][1];
    }
    tl_predicted(p1, p2, NULL, n);
    tl_set_iterations(qps);
    theta1_r = theta1_ref + ((step_time > 0 && t >= step_time) ? step : 0.0);
    theta2_w = x0[1];
}
