#ifndef TRACELAB_LF_PDCONTROLLER_H
#define TRACELAB_LF_PDCONTROLLER_H

// Line-follower benchmark: a PD on the front line sensor array, the classic
// line follower, as a baseline for the qLPV MPC and the GPC. Unlike them it
// needs no preview of the track: only the array's readings.
//
// The line's lateral position under the array is the readings' centroid,
//   e = sum(s_i * x_i) / sum(s_i),  x_i = (i - (n - 1) / 2) * spacing
// (m, car frame, > 0 = the line is to the right; s0 is the leftmost sensor).
// With the line lost (sum below `lostThreshold`) e holds the edge of the
// array on the side the line was last seen, so the car turns back to it.
//   delta = kp * e + kd * de/dt
//   u_l = base + delta,  u_r = base - delta   (%, saturated to [uMin, uMax])
// u_l > u_r turns right (omega = r / L * (w_l - w_r)).
//
// Plain C++17, no Qt and no Eigen: firmware-ready.

#include <array>
#include <vector>

namespace tracelab::lf {

struct PdParams {
    double kp = 1200.0;        // % per m of line offset
    double kd = 20.0;          // % per m/s
    double base = 35.0;        // % on both wheels
    double spacing = 0.008;    // m between sensors
    double lostThreshold = 0.05;
    double ts = 1.0 / 80.0;    // s
    double uMin = -100.0;
    double uMax = 100.0;
};

class PdController {
public:
    explicit PdController(const PdParams& params = {});

    // One sample: the array's readings (0..1, s0 leftmost). Returns
    // [u_l, u_r] in %.
    std::array<double, 2> step(const std::vector<double>& sensors);
    void reset();

    const PdParams& params() const { return m_params; }
    // The line offset used in the last step (m) and whether the line was seen.
    double error() const { return m_error; }
    bool lineSeen() const { return m_seen; }

private:
    PdParams m_params;
    double m_error = 0.0;
    double m_previous = 0.0;
    bool m_hasPrevious = false;
    bool m_seen = false;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_PDCONTROLLER_H
