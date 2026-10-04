#include <tracelab.h>

/* A plant: states, inputs and the measurements a real sensor would give.
   TraceLab integrates plant_deriv() over each sample period (the solver is
   picked on the plant's card) and publishes the outputs on loop.state. */

TL_PARAM(tau, 0.1, "s", 0.001, 10);
TL_PARAM(k, 1.0, "", 0, 100);

TL_INPUT(u, "%", -100, 100);

TL_STATE(w, "rad/s", 0);

TL_OUTPUT(w_meas, "rad/s");

void plant_deriv(void) {
    der(w) = (k * u - w) / tau;
}

void plant_output(void) {
    w_meas = w + tl_noise(0.01);
}
