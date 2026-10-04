#include <tracelab.h>

/* A filter: runs between the plant and the controller, on the signals by
   name. TL_INOUT replaces a signal; TL_OUT with a new name adds one. */

TL_PARAM(tau, 0.02, "s", 0, 1);

TL_INOUT(w_l);
TL_INOUT(w_r);

static double fl, fr;

void filt_reset(void) {
    fl = fr = 0;
}

void filt_step(void) {
    double a = tau > 0 ? tl_ts() / (tau + tl_ts()) : 1;
    fl += a * (w_l - fl);
    fr += a * (w_r - fr);
    w_l = fl;
    w_r = fr;
}
