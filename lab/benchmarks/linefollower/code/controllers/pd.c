#include <tracelab.h>

/* PD on the line sensor array: the classic line follower, and the baseline
 * for the MPCs. It needs no preview of the track, only the array.
 *
 * The line's lateral position under the array is the readings' centroid,
 *     e = sum(s_i * x_i) / sum(s_i),   x_i = (i - (N - 1) / 2) * spacing
 * (m, car frame, > 0 = the line is to the right; s[0] is the leftmost
 * sensor). With the line lost (sum below `lost`) e holds the edge of the
 * array on the side the line was last seen, so the car turns back to it.
 *     delta = kp * e + kd * de/dt
 *     u_l = base + delta,   u_r = base - delta     (%, saturated)
 * u_l > u_r turns right.
 */

TL_PARAM(kp, 2500, "%/m", 0, 1e5);
TL_PARAM(kd, 25, "%s/m", 0, 1e4);
TL_PARAM(base, 60, "%", -100, 100);
TL_PARAM(spacing, 0.008, "m", 0.001, 0.05);
TL_PARAM(lost, 0.05, "", 0, 15);

#define N 15                /* sensors in the array */

TL_IN_ARRAY(s, N);          /* line sensors, 0..1 */
TL_OUT(u_l);                /* left wheel, % */
TL_OUT(u_r);                /* right wheel, % */

TL_DIAG(line_offset, "m");  /* e */
TL_DIAG(line_seen, "");     /* 1 while the array sees the line */

static double previous;
static int has_previous;

void ctl_reset(void) {
    line_offset = 0;
    previous = 0;
    has_previous = 0;
}

void ctl_step(void) {
    double sum = 0, weighted = 0;
    for (int i = 0; i < N; i++) {
        double v = isfinite(s[i]) ? s[i] : 0;
        double x = (i - 0.5 * (N - 1)) * spacing;
        sum += v;
        weighted += v * x;
    }
    line_seen = sum >= lost;
    if (line_seen) {
        line_offset = weighted / sum;
    } else {
        double edge = 0.5 * (N - 1) * spacing;
        line_offset = line_offset >= 0 ? edge : -edge;
    }
    double derivative = has_previous ? (line_offset - previous) / tl_ts() : 0;
    previous = line_offset;
    has_previous = 1;

    double delta = kp * line_offset + kd * derivative;
    u_l = tl_sat(base + delta, -100, 100);
    u_r = tl_sat(base - delta, -100, 100);
}
