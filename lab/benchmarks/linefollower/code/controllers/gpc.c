#include <tracelab.h>

/* GPC linearized at theta = 0, built from the motor parameters (the port of
 * line_follower_simulator's mpc/gpc.py).
 *
 * Each wheel angle phi follows the CARIMA model (Delta u as input):
 *     (1 - a z^-1)(1 - z^-1)^2 phi(k) = Ts b z^-1 Delta u(k)
 * Outputs (car frame of k, linear in the wheel angles):
 *     theta = r/L (phi_l - phi_r),   d = r/2 (phi_l + phi_r)
 * Free response: the homogeneous recursion from the last 3 wheel angles;
 * forced response: each wheel's step response (Toeplitz, Nu columns).
 * Reference: theta_r = x / d0 ("lookahead") or the track tangent
 * ("tangent"), d_r = y of the preview. Unconstrained law
 *     Delta U = K (r - f),   K = (G'QG + lambda/Nu I)^-1 G'Q
 * and the input saturated afterwards. The horizon N is the number of preview
 * points the plant sends.
 */

TL_PARAM_INT(control_horizon, 5, 1, 32);              /* Nu */
TL_PARAM(q_theta, 0.5, "", 0, 1e6);
TL_PARAM(q_distance, 1.0, "", 0, 1e6);
TL_PARAM(lambda, 1e-3, "", 0, 1e3);
TL_PARAM(weight_growth, 1.0, "", 0.01, 100);          /* last step / first step */
TL_PARAM_ENUM(theta_reference, 0, "lookahead|tangent");
TL_PARAM(d0, 0.3, "m", 0.01, 5);
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

#define NMAX 64
#define NUMAX 32

TL_IN(w_l);                    /* wheel speeds, rad/s */
TL_IN(w_r);
TL_PREVIEW(px, py, NMAX);      /* the track ahead, car frame */
TL_OUT(u_l);                   /* % */
TL_OUT(u_r);

TL_DIAG(theta_ref, "rad");     /* heading aimed at on the first step */
TL_DIAG(theta_pred, "rad");    /* heading predicted there */
TL_DIAG(w_l_filt, "rad/s");    /* the wheel speeds it was given (after the filters) */
TL_DIAG(w_r_filt, "rad/s");

/* The model, rebuilt when N, Nu, Ts or a parameter it uses changes. */
static double built[16];
static int n_built, nu_built;
static double F[2][NMAX][3];           /* free response per wheel */
static double G[2 * NMAX][2 * NUMAX];  /* dynamic matrix */
static double K[2 * NUMAX][2 * NMAX];  /* gain */
static double c_theta, c_d;

/* The state between steps. */
static double u[2];
static double phi[2][3];       /* phi(k), phi(k-1), phi(k-2), relative to phi(k) */
static double freeresp[2 * NMAX];
static double du[2 * NUMAX];

static void build(int n, int nu) {
    static double T[2][NMAX][NUMAX];
    double ts = tl_ts();
    double ke = 2.0 * M_PI * wheel_rpm / 6000.0;
    double alpha[2], beta[2];
    alpha[0] = exp(-5.0 * ts / settle_left);
    alpha[1] = exp(-5.0 * ts / settle_right);
    beta[0] = ke * ke_left * (1.0 - alpha[0]);
    beta[1] = ke * ke_right * (1.0 - alpha[1]);
    c_theta = wheel_radius / wheel_distance;
    c_d = wheel_radius / 2.0;

    for (int w = 0; w < 2; w++) {
        /* A~(z^-1) = (1 - a z^-1)(1 - z^-1)^2 = 1 + a1 z^-1 + a2 z^-2 + a3 z^-3 */
        double a[3] = {-2.0 - alpha[w], 1.0 + 2.0 * alpha[w], -alpha[w]};
        double h0[3] = {1, 0, 0}, h1[3] = {0, 1, 0}, h2[3] = {0, 0, 1};
        for (int j = 0; j < n; j++) {
            double next[3];
            for (int i = 0; i < 3; i++) {
                next[i] = -(a[0] * h0[i] + a[1] * h1[i] + a[2] * h2[i]);
            }
            for (int i = 0; i < 3; i++) {
                F[w][j][i] = next[i];
                h2[i] = h1[i];
                h1[i] = h0[i];
                h0[i] = next[i];
            }
        }
        /* Step response: Delta u = 1 at k = 0. */
        double g[NMAX];
        double p0 = 0, p1 = 0, p2 = 0;
        for (int j = 0; j < n; j++) {
            double next = -(a[0] * p0 + a[1] * p1 + a[2] * p2) + (j == 0 ? ts * beta[w] : 0.0);
            g[j] = next;
            p2 = p1;
            p1 = p0;
            p0 = next;
        }
        /* Toeplitz: column i is g delayed by i. */
        memset(T[w], 0, sizeof T[w]);
        for (int i = 0; i < nu && i < n; i++) {
            for (int r = 0; r < n - i; r++) {
                T[w][i + r][i] = g[r];
            }
        }
    }

    /* G = [[c_theta G_l, -c_theta G_r], [c_d G_l, c_d G_r]] */
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < nu; i++) {
            G[j][i] = c_theta * T[0][j][i];
            G[j][nu + i] = -c_theta * T[1][j][i];
            G[n + j][i] = c_d * T[0][j][i];
            G[n + j][nu + i] = c_d * T[1][j][i];
        }
    }

    /* Exponential weighting along the horizon, q / N and lambda / Nu. */
    double q[2 * NMAX];
    for (int j = 0; j < n; j++) {
        double profile = pow(weight_growth, (double)j / (double)(n - 1 > 1 ? n - 1 : 1));
        q[j] = profile * q_theta / n;
        q[n + j] = profile * q_distance / n;
    }
    /* K = solve(G'QG + lambda/Nu I, G'Q), packed into contiguous arrays. */
    static double gp[2 * NMAX * 2 * NUMAX], gtqp[2 * NUMAX * 2 * NMAX], lhsp[4 * NUMAX * NUMAX];
    for (int r = 0; r < 2 * n; r++) {
        for (int c = 0; c < 2 * nu; c++) {
            gp[r * 2 * nu + c] = G[r][c];
        }
    }
    for (int r = 0; r < 2 * nu; r++) {
        for (int c = 0; c < 2 * n; c++) {
            gtqp[r * 2 * n + c] = G[c][r] * q[c];
        }
    }
    tl_mat_mul(gtqp, gp, lhsp, 2 * nu, 2 * n, 2 * nu);
    for (int i = 0; i < 2 * nu; i++) {
        lhsp[i * 2 * nu + i] += lambda / nu;
    }
    tl_mat_solve(lhsp, gtqp, 2 * nu, 2 * n);   /* gtqp becomes K */
    for (int r = 0; r < 2 * nu; r++) {
        for (int c = 0; c < 2 * n; c++) {
            K[r][c] = gtqp[r * 2 * n + c];
        }
    }
}

/* np.gradient (edge order 1). */
static void gradient(const double *f, double *df, int n) {
    df[0] = f[1] - f[0];
    df[n - 1] = f[n - 1] - f[n - 2];
    for (int i = 1; i < n - 1; i++) {
        df[i] = (f[i + 1] - f[i - 1]) / 2.0;
    }
}

void ctl_reset(void) {
    u[0] = u[1] = 0;
    memset(phi, 0, sizeof phi);
    n_built = nu_built = 0;
}

void ctl_step(void) {
    int n = tl_preview_count();
    int nu = control_horizon < NUMAX ? control_horizon : NUMAX;
    if (n < 2) {
        return;
    }
    double now[16] = {tl_ts(), q_theta, q_distance, lambda, weight_growth, wheel_radius,
                      wheel_distance, wheel_rpm, ke_left, ke_right, settle_left, settle_right};
    if (n != n_built || nu != nu_built || memcmp(now, built, sizeof now) != 0) {
        build(n, nu);
        n_built = n;
        nu_built = nu;
        memcpy(built, now, sizeof now);
    }

    /* Integrate the wheel angles, written relative to phi(k). */
    double w[2] = {w_l, w_r};
    for (int i = 0; i < 2; i++) {
        double phik = phi[i][0] + w[i] * tl_ts();
        double shifted[3] = {phik, phi[i][0], phi[i][1]};
        for (int k = 0; k < 3; k++) {
            phi[i][k] = shifted[k] - phik;
        }
    }

    /* Free response of each wheel, then [theta; d]. */
    for (int j = 0; j < n; j++) {
        double fl = F[0][j][0] * phi[0][0] + F[0][j][1] * phi[0][1] + F[0][j][2] * phi[0][2];
        double fr = F[1][j][0] * phi[1][0] + F[1][j][1] * phi[1][1] + F[1][j][2] * phi[1][2];
        freeresp[j] = c_theta * (fl - fr);
        freeresp[n + j] = c_d * (fl + fr);
    }

    /* Reference [theta_r; d_r]. */
    double dx[NMAX], dy[NMAX], r[2 * NMAX];
    gradient(px, dx, n);
    gradient(py, dy, n);
    for (int j = 0; j < n; j++) {
        r[j] = theta_reference == 0 ? px[j] / d0 : atan2(dx[j], dy[j]);
        r[n + j] = py[j];
    }

    /* Delta U = K (r - f) */
    for (int i = 0; i < 2 * nu; i++) {
        double sum = 0;
        for (int j = 0; j < 2 * n; j++) {
            sum += K[i][j] * (r[j] - freeresp[j]);
        }
        du[i] = sum;
    }
    u[0] = tl_sat(u[0] + du[0], u_min, u_max);
    u[1] = tl_sat(u[1] + du[nu], u_min, u_max);
    u_l = u[0];
    u_r = u[1];

    /* The prediction: theta and d from free + forced, X and Y integrated
       from them only for the scene. */
    double th[NMAX], x[NMAX], y[NMAX];
    double d_prev = 0, heading = 0, xs = 0, ys = 0;
    for (int j = 0; j < n; j++) {
        double yt = freeresp[j], yd = freeresp[n + j];
        for (int i = 0; i < 2 * nu; i++) {
            yt += G[j][i] * du[i];
            yd += G[n + j][i] * du[i];
        }
        double ds = yd - d_prev;
        xs += ds * sin(heading);
        ys += ds * cos(heading);
        th[j] = yt;
        x[j] = xs;
        y[j] = ys;
        d_prev = yd;
        heading = yt;
    }
    tl_predicted(x, y, th, n);
    theta_ref = r[0];
    theta_pred = th[0];
    w_l_filt = w_l;
    w_r_filt = w_r;
}
