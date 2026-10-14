#include <tracelab.h>

/* First-order low-pass on the measured wheel speeds (the encoders count
 * whole ticks, so their speed is a staircase):
 *     y += alpha * (x - y),   alpha = 1 - exp(-Ts / tau)
 * the exact discretization of 1 / (tau s + 1) under a held input. The first
 * sample after a reset sets y, so there is no start-up ramp. tau = 0 passes
 * the speeds through.
 *
 * TL_INOUT replaces w_l and w_r: whatever comes after this filter (another
 * filter, the controller) sees the filtered speeds under the same names.
 */

TL_PARAM(tau, 0.02, "s", 0, 1);

TL_INOUT(w_l);
TL_INOUT(w_r);

static double yl, yr;
static int primed;

void filt_reset(void) {
    yl = yr = 0;
    primed = 0;
}

void filt_step(void) {
    double alpha = tau > 0 ? 1 - exp(-tl_ts() / tau) : 1;
    if (!primed) {
        yl = w_l;
        yr = w_r;
        primed = 1;
    } else {
        yl += alpha * (w_l - yl);
        yr += alpha * (w_r - yr);
    }
    w_l = yl;
    w_r = yr;
}
