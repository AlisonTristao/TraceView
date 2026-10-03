/*
 * tlfilter.c -- see tlfilter.h. C99, no allocation, <math.h>/<string.h> only.
 */
#include "tlfilter.h"

#include <math.h>
#include <string.h>

#ifdef TLF_USE_DOUBLE
#define TLF_EXP(v) exp(v)
#define TLF_FABS(v) fabs(v)
#define TLF_PIVOT_EPS 1e-12
#else
#define TLF_EXP(v) expf(v)
#define TLF_FABS(v) fabsf(v)
#define TLF_PIVOT_EPS 1e-6f
#endif

#define N_MAX TLF_KF_MAX_N
#define M_MAX TLF_KF_MAX_M

/* ======================================================================== */
/* generic block, chain, bank                                               */
/* ======================================================================== */

tlf_block tlf_block_make(tlf_step_fn step, tlf_reset_fn reset, void* state, uint8_t n_in,
                         uint8_t n_out) {
    tlf_block b;
    b.step = step;
    b.reset = reset;
    b.state = state;
    b.n_in = n_in;
    b.n_out = n_out;
    return b;
}

int tlf_chain_init(tlf_chain* c, tlf_block* blocks, uint16_t count) {
    uint16_t i;
    if (!c || !blocks || count == 0) return TLF_EINVAL;
    for (i = 0; i < count; ++i) {
        if (!blocks[i].step || blocks[i].n_in == 0 || blocks[i].n_out == 0 ||
            blocks[i].n_in > TLF_MAX_CHANNELS || blocks[i].n_out > TLF_MAX_CHANNELS)
            return TLF_EINVAL;
        if (i > 0 && blocks[i - 1].n_out != blocks[i].n_in) return TLF_EINVAL;
    }
    memset(c, 0, sizeof(*c));
    c->blocks = blocks;
    c->count = count;
    c->n_in = blocks[0].n_in;
    c->n_out = blocks[count - 1].n_out;
    return TLF_OK;
}

void tlf_chain_step(tlf_chain* c, const tlf_real* in, tlf_real* out) {
    const tlf_real* src = in;
    uint16_t i;
    uint8_t k;
    /* Every block writes into one of the two internal buffers in turn, so a
     * block never sees in == out, and out may alias in. */
    for (i = 0; i < c->count; ++i) {
        tlf_real* dst = c->buf[i & 1u];
        c->blocks[i].step(c->blocks[i].state, src, dst);
        src = dst;
    }
    for (k = 0; k < c->n_out; ++k) out[k] = src[k];
}

void tlf_chain_reset(tlf_chain* c) {
    uint16_t i;
    for (i = 0; i < c->count; ++i)
        if (c->blocks[i].reset) c->blocks[i].reset(c->blocks[i].state);
    memset(c->buf, 0, sizeof(c->buf));
}

int tlf_bank_init(tlf_bank* b, tlf_block* blocks, uint8_t n) {
    uint8_t i;
    if (!b || !blocks || n == 0 || n > TLF_MAX_CHANNELS) return TLF_EINVAL;
    for (i = 0; i < n; ++i)
        if (!blocks[i].step || blocks[i].n_in != 1 || blocks[i].n_out != 1) return TLF_EINVAL;
    b->blocks = blocks;
    b->n = n;
    return TLF_OK;
}

static void bank_step(void* st, const tlf_real* in, tlf_real* out) {
    tlf_bank* b = (tlf_bank*)st;
    uint8_t i;
    for (i = 0; i < b->n; ++i) b->blocks[i].step(b->blocks[i].state, &in[i], &out[i]);
}

static void bank_reset(void* st) {
    tlf_bank* b = (tlf_bank*)st;
    uint8_t i;
    for (i = 0; i < b->n; ++i)
        if (b->blocks[i].reset) b->blocks[i].reset(b->blocks[i].state);
}

tlf_block tlf_bank_block(tlf_bank* b) {
    return tlf_block_make(bank_step, bank_reset, b, b->n, b->n);
}

/* ======================================================================== */
/* moving average                                                           */
/* ======================================================================== */

int tlf_movavg_init(tlf_movavg* s, tlf_real* buf, uint16_t len) {
    if (!s || !buf || len == 0) return TLF_EINVAL;
    s->buf = buf;
    s->len = len;
    tlf_movavg_reset(s);
    return TLF_OK;
}

void tlf_movavg_reset(tlf_movavg* s) {
    uint16_t i;
    for (i = 0; i < s->len; ++i) s->buf[i] = 0;
    s->sum = 0;
    s->idx = 0;
    s->count = 0;
}

tlf_real tlf_movavg_step(tlf_movavg* s, tlf_real x) {
    s->sum += x - s->buf[s->idx];
    s->buf[s->idx] = x;
    if (++s->idx == s->len) {
        uint16_t i;
        tlf_real sum = 0;
        s->idx = 0;
        /* Exact re-sum once per lap: running-sum round-off cannot grow. */
        for (i = 0; i < s->len; ++i) sum += s->buf[i];
        s->sum = sum;
    }
    if (s->count < s->len) ++s->count;
    return s->sum / (tlf_real)s->count;
}

static void movavg_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_movavg_step((tlf_movavg*)st, in[0]);
}
static void movavg_breset(void* st) { tlf_movavg_reset((tlf_movavg*)st); }
tlf_block tlf_movavg_block(tlf_movavg* s) {
    return tlf_block_make(movavg_bstep, movavg_breset, s, 1, 1);
}

/* ======================================================================== */
/* median                                                                   */
/* ======================================================================== */

int tlf_median_init(tlf_median* s, uint8_t len) {
    if (!s || len == 0 || len > TLF_MEDIAN_MAX) return TLF_EINVAL;
    s->len = len;
    tlf_median_reset(s);
    return TLF_OK;
}

void tlf_median_reset(tlf_median* s) {
    memset(s->ring, 0, sizeof(s->ring));
    memset(s->sorted, 0, sizeof(s->sorted));
    s->idx = 0;
    s->count = 0;
}

tlf_real tlf_median_step(tlf_median* s, tlf_real x) {
    uint8_t n = s->count;
    uint8_t i;
    if (n == s->len) {
        /* Window full: remove the oldest sample from the sorted copy. */
        tlf_real old = s->ring[s->idx];
        for (i = 0; i < n; ++i)
            if (s->sorted[i] == old) break;
        for (; i + 1 < n; ++i) s->sorted[i] = s->sorted[i + 1];
        --n;
    }
    /* Insertion into the sorted copy. */
    i = n;
    while (i > 0 && s->sorted[i - 1] > x) {
        s->sorted[i] = s->sorted[i - 1];
        --i;
    }
    s->sorted[i] = x;
    ++n;
    s->ring[s->idx] = x;
    if (++s->idx == s->len) s->idx = 0;
    s->count = n;
    if (n & 1u) return s->sorted[n / 2];
    return (s->sorted[n / 2 - 1] + s->sorted[n / 2]) * (tlf_real)0.5;
}

static void median_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_median_step((tlf_median*)st, in[0]);
}
static void median_breset(void* st) { tlf_median_reset((tlf_median*)st); }
tlf_block tlf_median_block(tlf_median* s) {
    return tlf_block_make(median_bstep, median_breset, s, 1, 1);
}

/* ======================================================================== */
/* first-order low-pass / high-pass                                         */
/* ======================================================================== */

int tlf_lowpass1_init(tlf_lowpass1* s, tlf_real tau, tlf_real ts, int prime) {
    if (!s || !(tau >= 0) || !(ts > 0)) return TLF_EINVAL;
    s->alpha = (tau == 0) ? (tlf_real)1 : (tlf_real)1 - TLF_EXP(-ts / tau);
    s->prime = prime ? 1u : 0u;
    tlf_lowpass1_reset(s);
    return TLF_OK;
}

void tlf_lowpass1_reset(tlf_lowpass1* s) {
    s->y = 0;
    s->primed = 0;
}

tlf_real tlf_lowpass1_step(tlf_lowpass1* s, tlf_real x) {
    if (s->prime && !s->primed) {
        s->primed = 1;
        s->y = x;
        return x;
    }
    s->y += s->alpha * (x - s->y);
    return s->y;
}

static void lowpass1_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_lowpass1_step((tlf_lowpass1*)st, in[0]);
}
static void lowpass1_breset(void* st) { tlf_lowpass1_reset((tlf_lowpass1*)st); }
tlf_block tlf_lowpass1_block(tlf_lowpass1* s) {
    return tlf_block_make(lowpass1_bstep, lowpass1_breset, s, 1, 1);
}

int tlf_highpass1_init(tlf_highpass1* s, tlf_real tau, tlf_real ts, int prime) {
    if (!s || !(tau > 0) || !(ts > 0)) return TLF_EINVAL;
    s->a = TLF_EXP(-ts / tau);
    s->prime = prime ? 1u : 0u;
    tlf_highpass1_reset(s);
    return TLF_OK;
}

void tlf_highpass1_reset(tlf_highpass1* s) {
    s->y = 0;
    s->x_prev = 0;
    s->primed = 0;
}

tlf_real tlf_highpass1_step(tlf_highpass1* s, tlf_real x) {
    if (s->prime && !s->primed) {
        s->primed = 1;
        s->x_prev = x;
        s->y = 0;
        return 0;
    }
    s->y = s->a * (s->y + x - s->x_prev);
    s->x_prev = x;
    return s->y;
}

static void highpass1_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_highpass1_step((tlf_highpass1*)st, in[0]);
}
static void highpass1_breset(void* st) { tlf_highpass1_reset((tlf_highpass1*)st); }
tlf_block tlf_highpass1_block(tlf_highpass1* s) {
    return tlf_block_make(highpass1_bstep, highpass1_breset, s, 1, 1);
}

/* ======================================================================== */
/* biquad cascade, Direct Form II transposed                                */
/* ======================================================================== */

int tlf_biquad_init(tlf_biquad* s, const tlf_biquad_coeffs* sections, uint8_t n) {
    uint8_t i;
    if (!s || !sections || n == 0 || n > TLF_BIQUAD_MAX_SECTIONS) return TLF_EINVAL;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < n; ++i) s->c[i] = sections[i];
    s->n = n;
    return TLF_OK;
}

void tlf_biquad_reset(tlf_biquad* s) {
    memset(s->z1, 0, sizeof(s->z1));
    memset(s->z2, 0, sizeof(s->z2));
}

tlf_real tlf_biquad_step(tlf_biquad* s, tlf_real x) {
    uint8_t i;
    for (i = 0; i < s->n; ++i) {
        const tlf_biquad_coeffs* c = &s->c[i];
        const tlf_real y = c->b0 * x + s->z1[i];
        s->z1[i] = c->b1 * x - c->a1 * y + s->z2[i];
        s->z2[i] = c->b2 * x - c->a2 * y;
        x = y;
    }
    return x;
}

static void biquad_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_biquad_step((tlf_biquad*)st, in[0]);
}
static void biquad_breset(void* st) { tlf_biquad_reset((tlf_biquad*)st); }
tlf_block tlf_biquad_block(tlf_biquad* s) {
    return tlf_block_make(biquad_bstep, biquad_breset, s, 1, 1);
}

/* ======================================================================== */
/* complementary filter                                                     */
/* ======================================================================== */

int tlf_complementary_init(tlf_complementary* s, tlf_real tau, tlf_real ts, tlf_real ki) {
    if (!s || !(tau >= 0) || !(ts > 0) || !(ki >= 0)) return TLF_EINVAL;
    s->alpha = ts / (tau + ts);
    s->ts = ts;
    s->ki = ki;
    tlf_complementary_reset(s);
    return TLF_OK;
}

void tlf_complementary_reset(tlf_complementary* s) {
    s->angle = 0;
    s->bias = 0;
    s->primed = 0;
}

tlf_real tlf_complementary_step(tlf_complementary* s, tlf_real rate, tlf_real absolute) {
    tlf_real pred, e;
    if (!s->primed) {
        s->primed = 1;
        s->angle = absolute;
        return absolute;
    }
    pred = s->angle + s->ts * (rate - s->bias);
    e = absolute - pred;
    s->angle = pred + s->alpha * e;
    s->bias -= s->ki * e;
    return s->angle;
}

static void complementary_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_complementary_step((tlf_complementary*)st, in[0], in[1]);
}
static void complementary_breset(void* st) { tlf_complementary_reset((tlf_complementary*)st); }
tlf_block tlf_complementary_block(tlf_complementary* s) {
    return tlf_block_make(complementary_bstep, complementary_breset, s, 2, 1);
}

/* ======================================================================== */
/* filtered derivative                                                      */
/* ======================================================================== */

int tlf_derivative_init(tlf_derivative* s, tlf_real tau, tlf_real ts) {
    if (!s || !(tau >= 0) || !(ts > 0)) return TLF_EINVAL;
    if (tau == 0) {
        s->c1 = 0;
        s->c2 = (tlf_real)1 / ts;
    } else {
        s->c1 = (2 * tau - ts) / (2 * tau + ts);
        s->c2 = (tlf_real)2 / (2 * tau + ts);
    }
    tlf_derivative_reset(s);
    return TLF_OK;
}

void tlf_derivative_reset(tlf_derivative* s) {
    s->y = 0;
    s->x_prev = 0;
    s->primed = 0;
}

tlf_real tlf_derivative_step(tlf_derivative* s, tlf_real x) {
    if (!s->primed) {
        s->primed = 1;
        s->x_prev = x;
        s->y = 0;
        return 0;
    }
    s->y = s->c1 * s->y + s->c2 * (x - s->x_prev);
    s->x_prev = x;
    return s->y;
}

static void derivative_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_derivative_step((tlf_derivative*)st, in[0]);
}
static void derivative_breset(void* st) { tlf_derivative_reset((tlf_derivative*)st); }
tlf_block tlf_derivative_block(tlf_derivative* s) {
    return tlf_block_make(derivative_bstep, derivative_breset, s, 1, 1);
}

/* ======================================================================== */
/* rate limiter, dead zone                                                  */
/* ======================================================================== */

int tlf_ratelimit_init(tlf_ratelimit* s, tlf_real rise, tlf_real fall, tlf_real ts, int prime) {
    if (!s || !(rise > 0) || !(fall > 0) || !(ts > 0)) return TLF_EINVAL;
    s->up = rise * ts;
    s->down = fall * ts;
    s->prime = prime ? 1u : 0u;
    tlf_ratelimit_reset(s);
    return TLF_OK;
}

void tlf_ratelimit_reset(tlf_ratelimit* s) {
    s->y = 0;
    s->primed = 0;
}

tlf_real tlf_ratelimit_step(tlf_ratelimit* s, tlf_real x) {
    tlf_real d;
    if (s->prime && !s->primed) {
        s->primed = 1;
        s->y = x;
        return x;
    }
    d = x - s->y;
    if (d > s->up) d = s->up;
    if (d < -s->down) d = -s->down;
    s->y += d;
    return s->y;
}

static void ratelimit_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_ratelimit_step((tlf_ratelimit*)st, in[0]);
}
static void ratelimit_breset(void* st) { tlf_ratelimit_reset((tlf_ratelimit*)st); }
tlf_block tlf_ratelimit_block(tlf_ratelimit* s) {
    return tlf_block_make(ratelimit_bstep, ratelimit_breset, s, 1, 1);
}

int tlf_deadzone_init(tlf_deadzone* s, tlf_real width, int shift) {
    if (!s || !(width >= 0)) return TLF_EINVAL;
    s->width = width;
    s->shift = shift ? width : (tlf_real)0;
    return TLF_OK;
}

tlf_real tlf_deadzone_step(const tlf_deadzone* s, tlf_real x) {
    if (x > s->width) return x - s->shift;
    if (x < -s->width) return x + s->shift;
    return 0;
}

static void deadzone_bstep(void* st, const tlf_real* in, tlf_real* out) {
    out[0] = tlf_deadzone_step((const tlf_deadzone*)st, in[0]);
}
tlf_block tlf_deadzone_block(tlf_deadzone* s) {
    return tlf_block_make(deadzone_bstep, NULL, s, 1, 1);
}

/* ======================================================================== */
/* Kalman core, shared by tlf_kf and tlf_ekf                                */
/* ======================================================================== */

/* P = F P F' + Q, then symmetrized. */
static void kf_predict_cov(uint8_t n, tlf_real (*P)[N_MAX], const tlf_real (*F)[N_MAX],
                           const tlf_real (*Q)[N_MAX]) {
    tlf_real T[N_MAX][N_MAX];
    uint8_t i, j, k;
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < n; ++k) acc += F[i][k] * P[k][j];
            T[i][j] = acc;
        }
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < n; ++k) acc += T[i][k] * F[j][k];
            P[i][j] = acc + Q[i][j];
        }
    for (i = 0; i < n; ++i)
        for (j = i + 1; j < n; ++j) {
            const tlf_real v = (tlf_real)0.5 * (P[i][j] + P[j][i]);
            P[i][j] = v;
            P[j][i] = v;
        }
}

/* The measurement update with the innovation y = z - h(x) already computed:
 * S = H P H' + R; K' = S^-1 (H P) (Gauss-Jordan, partial pivoting); x += K y;
 * P = (I - K H) P (I - K H)' + K R K' (Joseph), symmetrized. */
static int kf_update_core(uint8_t n, uint8_t m, tlf_real* x, tlf_real (*P)[N_MAX],
                          const tlf_real (*H)[N_MAX], const tlf_real (*R)[M_MAX],
                          const tlf_real* y) {
    /* aug = [S | H P], m x (m + n); after elimination its right part is K'. */
    tlf_real aug[M_MAX][M_MAX + N_MAX];
    tlf_real K[N_MAX][M_MAX];
    tlf_real A[N_MAX][N_MAX];
    tlf_real T[N_MAX][N_MAX];
    const uint8_t w = (uint8_t)(m + n);
    tlf_real scale = 0;
    uint8_t i, j, k;

    for (i = 0; i < m; ++i)
        for (j = 0; j < n; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < n; ++k) acc += H[i][k] * P[k][j];
            aug[i][m + j] = acc; /* H P */
        }
    for (i = 0; i < m; ++i)
        for (j = 0; j < m; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < n; ++k) acc += aug[i][m + k] * H[j][k];
            aug[i][j] = acc + R[i][j]; /* S */
        }
    for (i = 0; i < m; ++i)
        if (TLF_FABS(aug[i][i]) > scale) scale = TLF_FABS(aug[i][i]);
    if (!(scale > 0)) return TLF_ESINGULAR;

    for (i = 0; i < m; ++i) {
        uint8_t piv = i;
        tlf_real best = TLF_FABS(aug[i][i]);
        tlf_real d;
        for (k = (uint8_t)(i + 1); k < m; ++k)
            if (TLF_FABS(aug[k][i]) > best) {
                best = TLF_FABS(aug[k][i]);
                piv = k;
            }
        if (!(best > TLF_PIVOT_EPS * scale)) return TLF_ESINGULAR;
        if (piv != i)
            for (j = 0; j < w; ++j) {
                const tlf_real t = aug[i][j];
                aug[i][j] = aug[piv][j];
                aug[piv][j] = t;
            }
        d = aug[i][i];
        for (j = 0; j < w; ++j) aug[i][j] /= d;
        for (k = 0; k < m; ++k) {
            tlf_real f;
            if (k == i) continue;
            f = aug[k][i];
            if (f == 0) continue;
            for (j = 0; j < w; ++j) aug[k][j] -= f * aug[i][j];
        }
    }
    for (i = 0; i < n; ++i)
        for (j = 0; j < m; ++j) K[i][j] = aug[j][m + i];

    for (i = 0; i < n; ++i) {
        tlf_real acc = 0;
        for (j = 0; j < m; ++j) acc += K[i][j] * y[j];
        x[i] += acc;
    }

    /* A = I - K H */
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) {
            tlf_real acc = (i == j) ? (tlf_real)1 : (tlf_real)0;
            for (k = 0; k < m; ++k) acc -= K[i][k] * H[k][j];
            A[i][j] = acc;
        }
    /* T = A P */
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < n; ++k) acc += A[i][k] * P[k][j];
            T[i][j] = acc;
        }
    /* P = T A' + K R K'  (KR reuses aug's left block: row i of K R) */
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < n; ++k) acc += T[i][k] * A[j][k];
            P[i][j] = acc;
        }
    for (i = 0; i < n; ++i) {
        tlf_real kr[M_MAX];
        for (j = 0; j < m; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < m; ++k) acc += K[i][k] * R[k][j];
            kr[j] = acc;
        }
        for (j = 0; j < n; ++j) {
            tlf_real acc = 0;
            for (k = 0; k < m; ++k) acc += kr[k] * K[j][k];
            P[i][j] += acc;
        }
    }
    for (i = 0; i < n; ++i)
        for (j = (uint8_t)(i + 1); j < n; ++j) {
            const tlf_real v = (tlf_real)0.5 * (P[i][j] + P[j][i]);
            P[i][j] = v;
            P[j][i] = v;
        }
    return TLF_OK;
}

static int kf_dims_ok(uint8_t n, uint8_t m, uint8_t nu) {
    return n >= 1 && n <= N_MAX && m >= 1 && m <= M_MAX && nu <= TLF_KF_MAX_U &&
           n <= TLF_MAX_CHANNELS && m <= TLF_MAX_CHANNELS;
}

static void set_identity(uint8_t n, tlf_real (*P)[N_MAX]) {
    uint8_t i, j;
    for (i = 0; i < N_MAX; ++i)
        for (j = 0; j < N_MAX; ++j) P[i][j] = (i == j && i < n) ? (tlf_real)1 : (tlf_real)0;
}

/* ======================================================================== */
/* linear Kalman filter                                                     */
/* ======================================================================== */

int tlf_kf_init(tlf_kf* kf, uint8_t n, uint8_t m, uint8_t nu) {
    if (!kf || !kf_dims_ok(n, m, nu)) return TLF_EINVAL;
    memset(kf, 0, sizeof(*kf));
    kf->n = n;
    kf->m = m;
    kf->nu = nu;
    set_identity(n, kf->F);
    set_identity(n, kf->P);
    return TLF_OK;
}

void tlf_kf_predict(tlf_kf* kf, const tlf_real* u) {
    tlf_real xn[N_MAX];
    uint8_t i, j;
    for (i = 0; i < kf->n; ++i) {
        tlf_real acc = 0;
        for (j = 0; j < kf->n; ++j) acc += kf->F[i][j] * kf->x[j];
        if (u)
            for (j = 0; j < kf->nu; ++j) acc += kf->B[i][j] * u[j];
        xn[i] = acc;
    }
    for (i = 0; i < kf->n; ++i) kf->x[i] = xn[i];
    kf_predict_cov(kf->n, kf->P, (const tlf_real(*)[N_MAX])kf->F, (const tlf_real(*)[N_MAX])kf->Q);
}

int tlf_kf_update(tlf_kf* kf, const tlf_real* z) {
    uint8_t i, j;
    for (i = 0; i < kf->m; ++i) {
        tlf_real acc = 0;
        for (j = 0; j < kf->n; ++j) acc += kf->H[i][j] * kf->x[j];
        kf->y[i] = z[i] - acc;
    }
    return kf_update_core(kf->n, kf->m, kf->x, kf->P, (const tlf_real(*)[N_MAX])kf->H,
                          (const tlf_real(*)[M_MAX])kf->R, kf->y);
}

static void kf_bstep(void* st, const tlf_real* in, tlf_real* out) {
    tlf_kf* kf = (tlf_kf*)st;
    uint8_t i;
    tlf_kf_predict(kf, kf->nu ? kf->u : NULL);
    (void)tlf_kf_update(kf, in);
    for (i = 0; i < kf->n; ++i) out[i] = kf->x[i];
}
static void kf_breset(void* st) {
    tlf_kf* kf = (tlf_kf*)st;
    memset(kf->x, 0, sizeof(kf->x));
    memset(kf->y, 0, sizeof(kf->y));
    set_identity(kf->n, kf->P);
}
tlf_block tlf_kf_block(tlf_kf* kf) { return tlf_block_make(kf_bstep, kf_breset, kf, kf->m, kf->n); }

/* ======================================================================== */
/* extended Kalman filter                                                   */
/* ======================================================================== */

int tlf_ekf_init(tlf_ekf* ekf, uint8_t n, uint8_t m, uint8_t nu, tlf_ekf_f_fn f, tlf_ekf_h_fn h,
                 void* ctx) {
    if (!ekf || !f || !h || !kf_dims_ok(n, m, nu)) return TLF_EINVAL;
    memset(ekf, 0, sizeof(*ekf));
    ekf->n = n;
    ekf->m = m;
    ekf->nu = nu;
    ekf->f = f;
    ekf->h = h;
    ekf->ctx = ctx;
    set_identity(n, ekf->P);
    return TLF_OK;
}

void tlf_ekf_predict(tlf_ekf* ekf, const tlf_real* u) {
    tlf_real F[N_MAX][N_MAX];
    tlf_real xn[N_MAX];
    tlf_real u0[TLF_KF_MAX_U + 1];
    uint8_t i;
    memset(F, 0, sizeof(F));
    memset(xn, 0, sizeof(xn));
    if (!u) {
        memset(u0, 0, sizeof(u0));
        u = u0;
    }
    ekf->f(ekf->ctx, ekf->x, u, xn, F);
    for (i = 0; i < ekf->n; ++i) ekf->x[i] = xn[i];
    kf_predict_cov(ekf->n, ekf->P, (const tlf_real(*)[N_MAX])F, (const tlf_real(*)[N_MAX])ekf->Q);
}

int tlf_ekf_update(tlf_ekf* ekf, const tlf_real* z, const tlf_real* u) {
    tlf_real H[M_MAX][N_MAX];
    tlf_real zp[M_MAX];
    tlf_real u0[TLF_KF_MAX_U + 1];
    uint8_t i;
    memset(H, 0, sizeof(H));
    memset(zp, 0, sizeof(zp));
    if (!u) {
        memset(u0, 0, sizeof(u0));
        u = u0;
    }
    ekf->h(ekf->ctx, ekf->x, u, zp, H);
    for (i = 0; i < ekf->m; ++i) ekf->y[i] = z[i] - zp[i];
    return kf_update_core(ekf->n, ekf->m, ekf->x, ekf->P, (const tlf_real(*)[N_MAX])H,
                          (const tlf_real(*)[M_MAX])ekf->R, ekf->y);
}

static void ekf_bstep(void* st, const tlf_real* in, tlf_real* out) {
    tlf_ekf* ekf = (tlf_ekf*)st;
    uint8_t i;
    tlf_ekf_predict(ekf, ekf->u);
    (void)tlf_ekf_update(ekf, in, ekf->u);
    for (i = 0; i < ekf->n; ++i) out[i] = ekf->x[i];
}
static void ekf_breset(void* st) {
    tlf_ekf* ekf = (tlf_ekf*)st;
    memset(ekf->x, 0, sizeof(ekf->x));
    memset(ekf->y, 0, sizeof(ekf->y));
    set_identity(ekf->n, ekf->P);
}
tlf_block tlf_ekf_block(tlf_ekf* ekf) {
    return tlf_block_make(ekf_bstep, ekf_breset, ekf, ekf->m, ekf->n);
}
