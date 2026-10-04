/* tracelab.h -- what a TraceLab code block sees.
 *
 * A block is one C file: a plant, a filter or a controller. It DECLARES what
 * it reads, writes and lets you tune with the TL_* macros below, and defines
 * the functions of its kind. Every declaration is a plain C variable: TraceLab
 * fills the inputs before calling your function and reads the outputs after.
 *
 *   Plant       TL_PARAM*, TL_INPUT, TL_STATE, TL_OUTPUT[_ARRAY], TL_DIAG
 *               void plant_reset(void);    optional, after the states got x0
 *               void plant_deriv(void);    continuous: write der(x) for each state
 *               void plant_update(void);   or, with TL_DISCRETE: write next(x)
 *               void plant_output(void);   once per tick: write the outputs
 *   Filter      TL_PARAM*, TL_IN[_ARRAY], TL_OUT[_ARRAY], TL_INOUT[_ARRAY], TL_DIAG
 *               void filt_reset(void);     optional
 *               void filt_step(void);
 *   Controller  TL_PARAM*, TL_IN[_ARRAY], TL_OUT / TL_OUT_PLAN, TL_PREVIEW, TL_DIAG
 *               void ctl_reset(void);      optional
 *               void ctl_step(void);
 *
 * Signals are bound BY NAME: a controller's TL_IN(w_l) reads the plant's
 * TL_OUTPUT(w_l) (or what a filter made of it); its TL_OUT(u_l) drives the
 * plant's TL_INPUT(u_l). A filter's TL_INOUT(w_l) replaces w_l for whatever
 * comes after it; TL_OUT(w_l_f) adds a new signal instead.
 *
 * C99 as the Tiny C Compiler takes it, freestanding: the C library is the
 * subset declared here (math, string, malloc, printf to the TraceLab log).
 */
#ifndef TRACELAB_H
#define TRACELAB_H

#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

/* ------------------------------------------------------------------------ */
/* The C library subset                                                      */

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

double sin(double), cos(double), tan(double), asin(double), acos(double);
double atan(double), atan2(double, double), sinh(double), cosh(double), tanh(double);
double exp(double), log(double), log10(double), log2(double), pow(double, double);
double sqrt(double), cbrt(double), hypot(double, double), fabs(double);
double floor(double), ceil(double), round(double), trunc(double), fmod(double, double);
double fmin(double, double), fmax(double, double), copysign(double, double);
double ldexp(double, int), frexp(double, int *);
int isnan_tl(double);
#define isnan(x) isnan_tl(x)
#define isfinite(x) (!isnan_tl((x) - (x)))
int abs(int);

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);

/* printf and puts write to the block's log in the TraceLab tab. */
int printf(const char *, ...);
int puts(const char *);
int snprintf(char *, size_t, const char *, ...);

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_E 2.7182818284590452354
#define NAN (tl_nan())
#define INFINITY (1e300 * 1e300)

/* ------------------------------------------------------------------------ */
/* Declarations                                                              */

enum {
    TL_K_PARAM = 1,      /* double, tunable live */
    TL_K_PARAM_INT = 2,  /* int, tunable live */
    TL_K_PARAM_ENUM = 3, /* int index into options ("a|b|c") */
    TL_K_PARAM_BOOL = 4, /* int 0/1 */
    TL_K_INPUT = 10,     /* plant: an input (loop.u), saturated to [lo, hi] */
    TL_K_STATE = 11,     /* plant: a state, x0 = def */
    TL_K_OUTPUT = 12,    /* plant: a measurement (loop.state) */
    TL_K_DISCRETE = 13,  /* plant: next(x) instead of der(x) */
    TL_K_IN = 20,        /* filter/controller: a signal read */
    TL_K_OUT = 21,       /* filter/controller: a signal written */
    TL_K_INOUT = 22,     /* filter: a signal replaced */
    TL_K_PREVIEW = 23,   /* controller: sim.preview, car frame (privileged) */
    TL_K_DIAG = 30       /* any: published for the charts (ctl.diag, sim.states) */
};

/* One declaration as TraceLab reads it back after compiling (by the symbol's
   tl__decl_ prefix). Keep in sync with lab/code/codehostapi.h. */
typedef struct tl_decl {
    int kind;
    int count;           /* array length, 1 for a scalar */
    const char *name;
    const char *unit;
    const char *options; /* TL_PARAM_ENUM: "a|b|c" */
    double def, lo, hi;
    void *ptr;           /* the variable */
    void *ptr2;          /* a state's der()/next(); a preview's y array */
} tl_decl;

#define TL__DECL(tag, n, kind, count, unit, options, def, lo, hi, ptr, ptr2) \
    const tl_decl tl__decl_##tag##_##n = {kind, count, #n, unit, options, def, lo, hi, ptr, ptr2}

/* Parameters: shown next to the code, changed live without recompiling. The
   value in the code is the default; the one you set is kept in the project. */
#define TL_PARAM(n, def, unit, lo, hi) \
    double n = (def); TL__DECL(p, n, TL_K_PARAM, 1, unit, "", def, lo, hi, &n, 0)
#define TL_PARAM_INT(n, def, lo, hi) \
    int n = (def); TL__DECL(p, n, TL_K_PARAM_INT, 1, "", "", def, lo, hi, &n, 0)
#define TL_PARAM_ENUM(n, def, options) \
    int n = (def); TL__DECL(p, n, TL_K_PARAM_ENUM, 1, "", options, def, 0, 0, &n, 0)
#define TL_PARAM_BOOL(n, def) \
    int n = (def); TL__DECL(p, n, TL_K_PARAM_BOOL, 1, "", "", def, 0, 1, &n, 0)

/* Plant. der(x) is dx/dt (continuous) and next(x) is x[k+1] (TL_DISCRETE). */
#define TL_INPUT(n, unit, lo, hi) \
    double n; TL__DECL(u, n, TL_K_INPUT, 1, unit, "", 0, lo, hi, &n, 0)
#define TL_STATE(n, unit, x0) \
    double n = (x0); double tl__d_##n; \
    TL__DECL(x, n, TL_K_STATE, 1, unit, "", x0, 0, 0, &n, &tl__d_##n)
#define TL_OUTPUT(n, unit) \
    double n; TL__DECL(y, n, TL_K_OUTPUT, 1, unit, "", 0, 0, 0, &n, 0)
#define TL_OUTPUT_ARRAY(n, size, unit) \
    double n[size]; TL__DECL(y, n, TL_K_OUTPUT, size, unit, "", 0, 0, 0, n, 0)
#define TL_DISCRETE \
    TL__DECL(k, discrete, TL_K_DISCRETE, 1, "", "", 0, 0, 0, 0, 0)
#define der(n) tl__d_##n
#define next(n) tl__d_##n

/* Filter and controller signals. */
#define TL_IN(n) \
    double n; TL__DECL(i, n, TL_K_IN, 1, "", "", 0, 0, 0, &n, 0)
#define TL_IN_ARRAY(n, size) \
    double n[size]; TL__DECL(i, n, TL_K_IN, size, "", "", 0, 0, 0, n, 0)
#define TL_OUT(n) \
    double n; TL__DECL(o, n, TL_K_OUT, 1, "", "", 0, 0, 0, &n, 0)
#define TL_OUT_ARRAY(n, size) \
    double n[size]; TL__DECL(o, n, TL_K_OUT, size, "", "", 0, 0, 0, n, 0)
#define TL_INOUT(n) \
    double n; TL__DECL(io, n, TL_K_INOUT, 1, "", "", 0, 0, 0, &n, 0)
#define TL_INOUT_ARRAY(n, size) \
    double n[size]; TL__DECL(io, n, TL_K_INOUT, size, "", "", 0, 0, 0, n, 0)
/* A controller's input over the horizon: n[0] for the next tick, n[1] the
   one after... The whole plan goes in one loop.u. */
#define TL_OUT_PLAN(n, steps) TL_OUT_ARRAY(n, steps)
/* The plant's preview of the reference (sim.preview): up to `size` points
   in the car frame of this tick, x lateral right, y forward (m). Information
   a real robot may not have -- the controller's card says it uses it. */
#define TL_PREVIEW(x, y, size) \
    double x[size]; double y[size]; \
    TL__DECL(r, x, TL_K_PREVIEW, size, "m", "", 0, 0, 0, x, y)

/* A value published every tick for the charts. */
#define TL_DIAG(n, unit) \
    double n; TL__DECL(d, n, TL_K_DIAG, 1, unit, "", 0, 0, 0, &n, 0)

/* ------------------------------------------------------------------------ */
/* TraceLab functions                                                        */

double tl_ts(void);        /* sample period, s */
double tl_fps(void);       /* 1 / tl_ts() */
long long tl_tick(void);   /* this tick, 1 for the first step */
double tl_time(void);      /* s since the reset */
int tl_preview_count(void); /* valid TL_PREVIEW points this tick */

/* Reproducible noise: one stream per block, restarted on every reset. */
double tl_noise(double sigma); /* gaussian, zero mean */
double tl_rand(void);          /* uniform in [0, 1) */
double tl_nan(void);

/* Writes to the block's log in the TraceLab tab, like printf. */
void tl_log(const char *fmt, ...);

/* Controller: the path it predicts, car frame of this tick (x lateral
   right, y forward, m; heading rad), drawn by the scene. */
void tl_predicted(const double *x, const double *y, const double *theta, int n);
/* Plant: a disturbance the simulation injects (sim.disturb, the keys of the
   scene...), by channel; 0 when none. */
double tl_disturb(int channel);

/* Controller: the solver iterations of this step (ctl.diag's iterations
   field, which the scene shows). */
void tl_set_iterations(int n);

/* Dense linear algebra, compiled with optimization (Eigen). Matrices are
   row-major double arrays: element (i, j) of an r x c matrix is a[i*c + j]. */
/* C (n x m) = A (n x k) * B (k x m) */
void tl_mat_mul(const double *a, const double *b, double *c, int n, int k, int m);
/* C (n x m) = A' * B, A stored k x n, B k x m */
void tl_mat_mul_tn(const double *a, const double *b, double *c, int n, int k, int m);
/* Solves A X = B (A n x n, B n x m) by LU with partial pivoting; X replaces
   B. 0, or -1 when A is singular. */
int tl_mat_solve(const double *a, double *b, int n, int m);
/* min 1/2 x'Hx + g'x  s.t.  lb <= x <= ub  (H n x n, positive definite):
   a primal-dual active set warm started by x0 (NULL: none), with a projected
   gradient fallback. Writes x; returns the active-set iterations (50 when the
   fallback ran), -1 on bad arguments. */
int tl_qp_box(const double *h, const double *g, const double *lb, const double *ub,
              const double *x0, double *x, int n);

static inline double tl_sat(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline double tl_quantize(double v, double step) {
    return step > 0 ? step * floor(v / step + 0.5) : v;
}
static inline double tl_deadzone(double v, double width) {
    return v > width ? v - width : (v < -width ? v + width : 0.0);
}
static inline double tl_sign(double v) {
    return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0);
}
static inline double tl_wrap_angle(double a) {
    a = fmod(a + M_PI, 2 * M_PI);
    return a < 0 ? a + M_PI : a - M_PI;
}

#endif /* TRACELAB_H */
