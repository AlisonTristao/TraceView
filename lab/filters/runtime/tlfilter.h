/*
 * tlfilter.h -- TraceLab's portable filter runtime (lab/README.md, "Filters").
 *
 * Plain C99, written to be copied as is into robot firmware (ESP32 with
 * ESP-IDF, or any MCU):
 *   - no dynamic allocation: every block keeps its state in a struct the
 *     caller owns (static, global or on the stack); the few blocks that need a
 *     variable-size buffer (moving average) take caller-provided storage;
 *   - no libc beyond <math.h> and <string.h> (<stddef.h>/<stdint.h> are
 *     freestanding headers); exp() is only called by the *_init()
 *     functions, the steps use + - * / and the Kalman update fabs() (no
 *     sqrt, no explicit inverse); the EKF callbacks are yours;
 *   - tlf_real is float by default; define TLF_USE_DOUBLE (for the whole
 *     build, the CMake option TRACELAB_FILTERS_DOUBLE does it) for double.
 *
 * Every block has the same shape:
 *   tlf_xxx               its state (+ parameters), owned by the caller
 *   tlf_xxx_init(s, ...)  validates the parameters, clears the state;
 *                         returns TLF_OK or TLF_EINVAL
 *   tlf_xxx_step(s, x)    one sample (scalar blocks return the output)
 *   tlf_xxx_reset(s)      back to the state right after init, parameters kept
 *   tlf_xxx_block(s)      the same block behind the generic tlf_block
 *                         interface, to put it in a tlf_chain
 *
 * Memory per block (sizeof, R = sizeof(tlf_real): 4 for float, 8 for double;
 * pointers 4 bytes on the ESP32):
 *   tlf_movavg        pointer + R + 6 bytes (16 B); plus the caller buffer of len reals
 *   tlf_median        2 * TLF_MEDIAN_MAX reals + 3 bytes   (31: 248 B float)
 *   tlf_lowpass1      2R + 1                               (~12 B float)
 *   tlf_highpass1     3R + 1                               (~16 B float)
 *   tlf_biquad        7R * TLF_BIQUAD_MAX_SECTIONS + 1     (8: ~228 B float)
 *   tlf_complementary 5R + 1                               (~24 B float)
 *   tlf_derivative    4R + 1                               (~20 B float)
 *   tlf_ratelimit     4R + 1                               (~20 B float)
 *   tlf_deadzone      2R                                   (8 B float)
 *   tlf_kf            see tlf_kf below (defaults: ~700 B float)
 *   tlf_ekf           see tlf_ekf below (defaults: ~416 B float)
 *   tlf_chain         2 * TLF_MAX_CHANNELS reals + pointer + 4 bytes
 * The Kalman updates also use stack temporaries, O(N*N + M*(M+N)) reals
 * (defaults: under 1 KB float); nothing else uses more than a few reals.
 */
#ifndef TLFILTER_H
#define TLFILTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- configuration (override with -D or before including) --------------- */

#ifdef TLF_USE_DOUBLE
typedef double tlf_real;
#else
typedef float tlf_real;
#endif

/* Widest vector that flows between two blocks of a chain. */
#ifndef TLF_MAX_CHANNELS
#define TLF_MAX_CHANNELS 8
#endif
/* Largest median window (the state is a fixed array of this size). */
#ifndef TLF_MEDIAN_MAX
#define TLF_MEDIAN_MAX 31
#endif
/* Second-order sections of one biquad cascade: an order-2N low/high-pass or an
 * order-N band-pass/band-stop needs N of them. */
#ifndef TLF_BIQUAD_MAX_SECTIONS
#define TLF_BIQUAD_MAX_SECTIONS 8
#endif
/* Kalman filter maximum dimensions: states, measurements, inputs. The
 * matrices are fixed arrays of these sizes; the actual n, m, nu are chosen at
 * init and may be smaller. */
#ifndef TLF_KF_MAX_N
#define TLF_KF_MAX_N 6
#endif
#ifndef TLF_KF_MAX_M
#define TLF_KF_MAX_M 4
#endif
#ifndef TLF_KF_MAX_U
#define TLF_KF_MAX_U 2
#endif

typedef enum tlf_status {
    TLF_OK = 0,
    TLF_EINVAL = -1,    /* a parameter is out of range */
    TLF_ESINGULAR = -2  /* Kalman update: innovation covariance not invertible */
} tlf_status;

/* ---- the generic block and the chain ------------------------------------- */

/* One step: read n_in values from in, write n_out values to out (in and out
 * never alias when called by a chain). */
typedef void (*tlf_step_fn)(void* state, const tlf_real* in, tlf_real* out);
typedef void (*tlf_reset_fn)(void* state);

typedef struct tlf_block {
    tlf_step_fn step;
    tlf_reset_fn reset; /* may be NULL */
    void* state;
    uint8_t n_in;
    uint8_t n_out;
} tlf_block;

/* A block of your own: function pointer + your state. This IS the user block:
 * a hand-written filter is any step function with this signature. */
tlf_block tlf_block_make(tlf_step_fn step, tlf_reset_fn reset, void* state, uint8_t n_in,
                         uint8_t n_out);

/* Blocks in series: block i's output is block i+1's input, so n_out of one
 * must equal n_in of the next, and every width must be <= TLF_MAX_CHANNELS.
 * The block array is the caller's and must outlive the chain. */
typedef struct tlf_chain {
    tlf_block* blocks;
    uint16_t count;
    uint8_t n_in;
    uint8_t n_out;
    tlf_real buf[2][TLF_MAX_CHANNELS];
} tlf_chain;

int tlf_chain_init(tlf_chain* c, tlf_block* blocks, uint16_t count);
/* in: c->n_in values, out: c->n_out values (out may alias in). */
void tlf_chain_step(tlf_chain* c, const tlf_real* in, tlf_real* out);
void tlf_chain_reset(tlf_chain* c);

/* A bank: n scalar (1 -> 1) blocks side by side, channel i through block i,
 * so a vector chain can filter each channel with its own block. */
typedef struct tlf_bank {
    tlf_block* blocks;
    uint8_t n;
} tlf_bank;
int tlf_bank_init(tlf_bank* b, tlf_block* blocks, uint8_t n);
tlf_block tlf_bank_block(tlf_bank* b);

/* ---- moving average (circular buffer) ------------------------------------ */

/* Mean of the last len samples (of the samples so far while fewer than len
 * have arrived). Running sum, recomputed exactly every time the ring wraps so
 * float round-off cannot accumulate: O(1) per sample, O(len) once per len
 * samples. buf (len reals) is caller storage. */
typedef struct tlf_movavg {
    tlf_real* buf;
    tlf_real sum;
    uint16_t len;
    uint16_t idx;
    uint16_t count;
} tlf_movavg;
int tlf_movavg_init(tlf_movavg* s, tlf_real* buf, uint16_t len);
tlf_real tlf_movavg_step(tlf_movavg* s, tlf_real x);
void tlf_movavg_reset(tlf_movavg* s);
tlf_block tlf_movavg_block(tlf_movavg* s);

/* ---- median (small fixed window) ----------------------------------------- */

/* Median of the last len samples, 1 <= len <= TLF_MEDIAN_MAX (of the samples
 * so far during warm-up). An even count gives the mean of the two middle
 * values. Keeps a sorted copy: O(len) per sample. */
typedef struct tlf_median {
    tlf_real ring[TLF_MEDIAN_MAX];
    tlf_real sorted[TLF_MEDIAN_MAX];
    uint8_t len;
    uint8_t idx;
    uint8_t count;
} tlf_median;
int tlf_median_init(tlf_median* s, uint8_t len);
tlf_real tlf_median_step(tlf_median* s, tlf_real x);
void tlf_median_reset(tlf_median* s);
tlf_block tlf_median_block(tlf_median* s);

/* ---- first-order low-pass (exponential smoothing) ------------------------ */

/* y += alpha (x - y), alpha = 1 - exp(-ts / tau): the exact discretization of
 * 1 / (tau s + 1) under a held input, so the step response is
 * 1 - exp(-k ts / tau) sample for sample. tau = 0 passes x through.
 * prime != 0: the first sample after init/reset sets y (no start-up ramp);
 * prime == 0: y starts at 0. */
typedef struct tlf_lowpass1 {
    tlf_real alpha;
    tlf_real y;
    uint8_t prime;
    uint8_t primed;
} tlf_lowpass1;
int tlf_lowpass1_init(tlf_lowpass1* s, tlf_real tau, tlf_real ts, int prime);
tlf_real tlf_lowpass1_step(tlf_lowpass1* s, tlf_real x);
void tlf_lowpass1_reset(tlf_lowpass1* s);
tlf_block tlf_lowpass1_block(tlf_lowpass1* s);

/* ---- first-order high-pass ----------------------------------------------- */

/* y = a (y + x - x_prev), a = exp(-ts / tau): the RC high-pass, whose step
 * response decays as exp(-k ts / tau) and which removes DC. prime != 0: the
 * first sample only sets x_prev (no kick from the initial level); prime == 0:
 * x_prev starts at 0. */
typedef struct tlf_highpass1 {
    tlf_real a;
    tlf_real y;
    tlf_real x_prev;
    uint8_t prime;
    uint8_t primed;
} tlf_highpass1;
int tlf_highpass1_init(tlf_highpass1* s, tlf_real tau, tlf_real ts, int prime);
tlf_real tlf_highpass1_step(tlf_highpass1* s, tlf_real x);
void tlf_highpass1_reset(tlf_highpass1* s);
tlf_block tlf_highpass1_block(tlf_highpass1* s);

/* ---- biquad cascade (Direct Form II transposed) -------------------------- */

/* One section, normalized so a0 = 1:
 *   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
 * Every IIR design of lab/filters/design (Butterworth, Chebyshev I/II,
 * notch...) comes out as an array of these; the overall gain is folded into
 * the sections. */
typedef struct tlf_biquad_coeffs {
    tlf_real b0, b1, b2, a1, a2;
} tlf_biquad_coeffs;

typedef struct tlf_biquad {
    tlf_biquad_coeffs c[TLF_BIQUAD_MAX_SECTIONS];
    tlf_real z1[TLF_BIQUAD_MAX_SECTIONS];
    tlf_real z2[TLF_BIQUAD_MAX_SECTIONS];
    uint8_t n;
} tlf_biquad;
/* Copies n sections (1 <= n <= TLF_BIQUAD_MAX_SECTIONS) and clears the state. */
int tlf_biquad_init(tlf_biquad* s, const tlf_biquad_coeffs* sections, uint8_t n);
tlf_real tlf_biquad_step(tlf_biquad* s, tlf_real x);
void tlf_biquad_reset(tlf_biquad* s);
tlf_block tlf_biquad_block(tlf_biquad* s);

/* ---- complementary filter ------------------------------------------------ */

/* Fuses a rate (gyro, in[0]) with a slow absolute measurement of the same
 * angle (accelerometer tilt, line position, compass..., in[1]):
 *   pred  = angle + ts (rate - bias)
 *   e     = absolute - pred
 *   angle = pred + alpha e,     alpha = ts / (tau + ts)
 *   bias -= ki e                (ki = 0: the classic first-order filter)
 * tau is the crossover time constant: the rate is trusted below it, the
 * absolute source above. With ki = 0 a constant gyro bias b leaves a
 * steady-state error of about b tau; ki > 0 estimates the bias (s->bias) and
 * drives that error to 0 (ki in 1/s per sample; the error loop is
 * s^2 + (alpha/ts) s + ki/ts, critically damped at ki = alpha^2 / (4 ts), and
 * a smaller ki gives a slower but quieter bias estimate). The first sample
 * sets angle = absolute. One output: the angle. */
typedef struct tlf_complementary {
    tlf_real alpha;
    tlf_real ts;
    tlf_real ki;
    tlf_real angle;
    tlf_real bias;
    uint8_t primed;
} tlf_complementary;
int tlf_complementary_init(tlf_complementary* s, tlf_real tau, tlf_real ts, tlf_real ki);
tlf_real tlf_complementary_step(tlf_complementary* s, tlf_real rate, tlf_real absolute);
void tlf_complementary_reset(tlf_complementary* s);
tlf_block tlf_complementary_block(tlf_complementary* s); /* 2 in, 1 out */

/* ---- filtered derivative ------------------------------------------------- */

/* s / (tau s + 1) by the bilinear (Tustin) transform:
 *   y = c1 y + c2 (x - x_prev), c1 = (2 tau - ts) / (2 tau + ts), c2 = 2 / (2 tau + ts).
 * A ramp of slope m gives y -> m. tau = 0: backward difference (x - x_prev)/ts.
 * The first sample only sets x_prev (output 0). */
typedef struct tlf_derivative {
    tlf_real c1;
    tlf_real c2;
    tlf_real y;
    tlf_real x_prev;
    uint8_t primed;
} tlf_derivative;
int tlf_derivative_init(tlf_derivative* s, tlf_real tau, tlf_real ts);
tlf_real tlf_derivative_step(tlf_derivative* s, tlf_real x);
void tlf_derivative_reset(tlf_derivative* s);
tlf_block tlf_derivative_block(tlf_derivative* s);

/* ---- rate limiter -------------------------------------------------------- */

/* y moves towards x by at most rise * ts per sample upwards and fall * ts
 * downwards (rise, fall > 0, units per second). prime != 0: the first sample
 * sets y; prime == 0: y starts at 0. */
typedef struct tlf_ratelimit {
    tlf_real up;   /* rise * ts */
    tlf_real down; /* fall * ts */
    tlf_real y;
    uint8_t prime;
    uint8_t primed;
} tlf_ratelimit;
int tlf_ratelimit_init(tlf_ratelimit* s, tlf_real rise, tlf_real fall, tlf_real ts, int prime);
tlf_real tlf_ratelimit_step(tlf_ratelimit* s, tlf_real x);
void tlf_ratelimit_reset(tlf_ratelimit* s);
tlf_block tlf_ratelimit_block(tlf_ratelimit* s);

/* ---- dead zone ----------------------------------------------------------- */

/* |x| <= width -> 0. Outside: shift != 0 gives x - sign(x) width (continuous,
 * the usual actuator dead-zone compensation inverse); shift == 0 passes x. */
typedef struct tlf_deadzone {
    tlf_real width;
    tlf_real shift;
} tlf_deadzone;
int tlf_deadzone_init(tlf_deadzone* s, tlf_real width, int shift);
tlf_real tlf_deadzone_step(const tlf_deadzone* s, tlf_real x);
tlf_block tlf_deadzone_block(tlf_deadzone* s);

/* ---- linear Kalman filter ------------------------------------------------ */

/* x_k+1 = F x_k + B u_k + w (cov Q),  z_k = H x_k + v (cov R).
 *
 * Same formulation and call shape as TinyEKF (github AlisonTristao/TinyEKF,
 * the library the robot firmware bally_OS uses): predict(u) then update(z);
 *   predict:  x = F x + B u,       P = F P F' + Q
 *   update:   y = z - H x,         S = H P H' + R,      K = P H' S^-1
 *             x = x + K y,
 *             P = (I - K H) P (I - K H)' + K R K'   (Joseph form, then
 *                                                    symmetrized)
 * S^-1 is never formed: K' = S^-1 (H P) by Gauss-Jordan with partial pivoting
 * (TinyEKF inverts S with the same elimination). A singular S skips the
 * measurement and returns TLF_ESINGULAR, as TinyEKF does. The one deliberate
 * difference: TinyEKF updates P with the short form P - K H P, which loses
 * symmetry and positive definiteness in float; Joseph form does not.
 *
 * Fill F, B, H, Q, R, x and P directly after tlf_kf_init() (which sets
 * F = I, P = I and the rest to 0). Only the top-left n x n / m x n / ... part
 * of each fixed array is used.
 * Memory: (N + 3 N^2 + N U + M N + M^2 + U + M) reals, i.e. with the default
 * N=6, M=4, U=2: 172 reals (688 B float). */
typedef struct tlf_kf {
    uint8_t n, m, nu;
    tlf_real x[TLF_KF_MAX_N];
    tlf_real P[TLF_KF_MAX_N][TLF_KF_MAX_N];
    tlf_real F[TLF_KF_MAX_N][TLF_KF_MAX_N];
    tlf_real B[TLF_KF_MAX_N][TLF_KF_MAX_U];
    tlf_real H[TLF_KF_MAX_M][TLF_KF_MAX_N];
    tlf_real Q[TLF_KF_MAX_N][TLF_KF_MAX_N];
    tlf_real R[TLF_KF_MAX_M][TLF_KF_MAX_M];
    tlf_real u[TLF_KF_MAX_U]; /* the input the chain block predicts with */
    tlf_real y[TLF_KF_MAX_M]; /* last innovation z - H x (diagnostics) */
} tlf_kf;
int tlf_kf_init(tlf_kf* kf, uint8_t n, uint8_t m, uint8_t nu);
void tlf_kf_predict(tlf_kf* kf, const tlf_real* u); /* u: nu values, or NULL = 0 */
int tlf_kf_update(tlf_kf* kf, const tlf_real* z);  /* TLF_OK or TLF_ESINGULAR */
/* Chain block: in = z (m values), out = x (n values); each step is
 * predict(kf->u) followed by update(z). Reset restores x = 0, P = I. */
tlf_block tlf_kf_block(tlf_kf* kf);

/* ---- extended Kalman filter ---------------------------------------------- */

/* x_k+1 = f(x_k, u_k) + w,  z_k = h(x_k, u_k) + v. The model is two callbacks
 * that return the value and its Jacobian, where TinyEKF generates both with
 * SymPy (optimizer.py) and compiles them in -- the equations, the predict(u) /
 * update(z, u) calls and the Joseph-form update are the same as tlf_kf's, so
 * a filter tuned here is moved to TinyEKF by pasting f and h into
 * optimizer.py (or kept in C with these callbacks).
 *   f: x_next = f(x, u), F = df/dx (n x n, rows of TLF_KF_MAX_N)
 *   h: z_pred = h(x, u), H = dh/dx (m x n)
 * Memory: (N + 2 N^2 + M^2 + U + M) reals + 3 pointers; defaults: 100 reals
 * (~416 B float). */
typedef void (*tlf_ekf_f_fn)(void* ctx, const tlf_real* x, const tlf_real* u, tlf_real* x_next,
                             tlf_real F[][TLF_KF_MAX_N]);
typedef void (*tlf_ekf_h_fn)(void* ctx, const tlf_real* x, const tlf_real* u, tlf_real* z_pred,
                             tlf_real H[][TLF_KF_MAX_N]);

typedef struct tlf_ekf {
    uint8_t n, m, nu;
    tlf_real x[TLF_KF_MAX_N];
    tlf_real P[TLF_KF_MAX_N][TLF_KF_MAX_N];
    tlf_real Q[TLF_KF_MAX_N][TLF_KF_MAX_N];
    tlf_real R[TLF_KF_MAX_M][TLF_KF_MAX_M];
    tlf_real u[TLF_KF_MAX_U];
    tlf_real y[TLF_KF_MAX_M];
    tlf_ekf_f_fn f;
    tlf_ekf_h_fn h;
    void* ctx;
} tlf_ekf;
int tlf_ekf_init(tlf_ekf* ekf, uint8_t n, uint8_t m, uint8_t nu, tlf_ekf_f_fn f, tlf_ekf_h_fn h,
                 void* ctx);
void tlf_ekf_predict(tlf_ekf* ekf, const tlf_real* u);
int tlf_ekf_update(tlf_ekf* ekf, const tlf_real* z, const tlf_real* u);
tlf_block tlf_ekf_block(tlf_ekf* ekf); /* in = z, out = x; predict + update with ekf->u */

#ifdef __cplusplus
}
#endif

#endif /* TLFILTER_H */
