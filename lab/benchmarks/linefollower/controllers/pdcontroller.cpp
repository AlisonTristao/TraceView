#include "pdcontroller.h"

#include <algorithm>
#include <cmath>

namespace tracelab::lf {

PdController::PdController(const PdParams& params) : m_params(params) {}

std::array<double, 2> PdController::step(const std::vector<double>& sensors) {
    const int n = int(sensors.size());
    double sum = 0.0;
    double weighted = 0.0;
    for (int i = 0; i < n; ++i) {
        const double s = std::isfinite(sensors[std::size_t(i)]) ? sensors[std::size_t(i)] : 0.0;
        const double x = (i - 0.5 * (n - 1)) * m_params.spacing;
        sum += s;
        weighted += s * x;
    }
    m_seen = sum >= m_params.lostThreshold;
    if (m_seen) {
        m_error = weighted / sum;
    } else {
        // Lost: the edge of the array, on the side it was last seen.
        const double edge = 0.5 * (n - 1) * m_params.spacing;
        m_error = m_error >= 0.0 ? edge : -edge;
    }
    const double derivative = m_hasPrevious ? (m_error - m_previous) / m_params.ts : 0.0;
    m_previous = m_error;
    m_hasPrevious = true;
    const double delta = m_params.kp * m_error + m_params.kd * derivative;
    return {std::clamp(m_params.base + delta, m_params.uMin, m_params.uMax),
            std::clamp(m_params.base - delta, m_params.uMin, m_params.uMax)};
}

void PdController::reset() {
    m_error = 0.0;
    m_previous = 0.0;
    m_hasPrevious = false;
    m_seen = false;
}

}  // namespace tracelab::lf
