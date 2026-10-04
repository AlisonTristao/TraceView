#include <tracelab.h>

/* A controller: reads the plant's measurements by name, writes its inputs.
   Parameters appear next to the code and change live. */

TL_PARAM(gain, 1.0, "", 0, 100);

/* What it reads (a TL_OUTPUT of the plant, or a filter's output)... */
TL_IN(w_l);
TL_IN(w_r);

/* ...and what it drives (a TL_INPUT of the plant). */
TL_OUT(u_l);
TL_OUT(u_r);

/* Published every tick, for the charts. */
TL_DIAG(error, "rad/s");

void ctl_reset(void) {
    /* back to the initial state: the simulation was reset or the code saved */
}

void ctl_step(void) {
    error = w_l - w_r;
    u_l = 50 - gain * error;
    u_r = 50 + gain * error;
}
