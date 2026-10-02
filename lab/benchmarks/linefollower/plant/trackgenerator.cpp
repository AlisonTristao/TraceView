#include "trackgenerator.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>

#include "pyrandom.h"

namespace tracelab::lf {

namespace {

constexpr double kPi = 3.14159265358979323846;  // math.pi

// Solves a * x = b in place (dense, partial pivoting); n is small (the
// number of checkpoints).
std::vector<double> solveDense(std::vector<double> a, std::vector<double> b, int n) {
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row) {
            if (std::fabs(a[row * n + col]) > std::fabs(a[pivot * n + col])) {
                pivot = row;
            }
        }
        if (pivot != col) {
            for (int c = 0; c < n; ++c) {
                std::swap(a[col * n + c], a[pivot * n + c]);
            }
            std::swap(b[col], b[pivot]);
        }
        const double diag = a[col * n + col];
        for (int row = col + 1; row < n; ++row) {
            const double f = a[row * n + col] / diag;
            if (f == 0.0) {
                continue;
            }
            for (int c = col; c < n; ++c) {
                a[row * n + c] -= f * a[col * n + c];
            }
            b[row] -= f * b[col];
        }
    }
    std::vector<double> x(n);
    for (int row = n - 1; row >= 0; --row) {
        double s = b[row];
        for (int c = row + 1; c < n; ++c) {
            s -= a[row * n + c] * x[c];
        }
        x[row] = s / a[row * n + row];
    }
    return x;
}

}  // namespace

std::vector<Point2> trackCheckpoints(const TrackParams& params, PyRandom& random) {
    const int count = params.checkpoints;
    const double radius = params.trackRad;
    const double noise = params.noiseLevel * params.trackRad;
    std::vector<Point2> points;
    points.reserve(std::size_t(std::max(count, 0)));
    for (int c = 0; c < count; ++c) {
        const double t = 2 * kPi * c / count;
        double x;
        double y;
        if (params.type == TrackType::Circle) {
            x = radius * std::cos(t);
            y = radius * std::sin(t);
            x += random.uniform(noise / 2, noise);
            y += random.uniform(noise / 2, noise);
        } else {
            x = radius * std::cos(t);
            y = radius * std::sin(t) * std::cos(t);
            x += random.uniform(noise / 3, noise);
            y += random.uniform(noise / 3, noise);
        }
        points.push_back({x, y});
    }
    return points;
}

void periodicSpline(const std::vector<Point2>& input, int samples, std::vector<double>* outX,
                    std::vector<double>* outY) {
    const int m = int(input.size());
    if (m < 3 || samples < 1) {
        throw std::invalid_argument("periodicSpline: at least 3 points and 1 sample");
    }
    // splprep(per=True): x[i][-1] = x[i][0] for each coordinate that differs.
    std::vector<Point2> p = input;
    if (p[0].x != p[m - 1].x) {
        p[m - 1].x = p[0].x;
    }
    if (p[0].y != p[m - 1].y) {
        p[m - 1].y = p[0].y;
    }

    // clocur (ipar = 0): cumulative chord length, divided by the total.
    std::vector<double> u(m, 0.0);
    for (int i = 1; i < m; ++i) {
        const double dx = p[i].x - p[i - 1].x;
        const double dy = p[i].y - p[i - 1].y;
        u[i] = u[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
    const double total = u[m - 1];
    if (!(total > 0.0)) {
        throw std::invalid_argument("periodicSpline: degenerate points");
    }
    for (int i = 1; i < m; ++i) {
        u[i] = u[i] / total;
    }

    // Periodic cubic spline on the n = m - 1 intervals: second derivatives
    // M_i (i mod n) from
    //   h_{i-1} M_{i-1} + 2 (h_{i-1} + h_i) M_i + h_i M_{i+1}
    //     = 6 ((y_{i+1} - y_i) / h_i - (y_i - y_{i-1}) / h_{i-1}).
    const int n = m - 1;
    std::vector<double> h(n);
    for (int i = 0; i < n; ++i) {
        h[i] = u[i + 1] - u[i];
    }
    std::vector<double> a(std::size_t(n) * n, 0.0);
    for (int i = 0; i < n; ++i) {
        const double hp = h[(i - 1 + n) % n];
        const double hc = h[i];
        a[i * n + (i - 1 + n) % n] += hp;
        a[i * n + i] += 2 * (hp + hc);
        a[i * n + (i + 1) % n] += hc;
    }
    const auto secondDerivatives = [&](const std::vector<double>& y) {
        std::vector<double> rhs(n);
        for (int i = 0; i < n; ++i) {
            const double hp = h[(i - 1 + n) % n];
            rhs[i] = 6 * ((y[(i + 1) % n] - y[i]) / h[i] - (y[i] - y[(i - 1 + n) % n]) / hp);
        }
        return solveDense(a, rhs, n);
    };
    std::vector<double> ys[2] = {std::vector<double>(n), std::vector<double>(n)};
    for (int i = 0; i < n; ++i) {
        ys[0][i] = p[i].x;
        ys[1][i] = p[i].y;
    }
    const std::vector<double> ms[2] = {secondDerivatives(ys[0]), secondDerivatives(ys[1])};

    // np.linspace(0, 1, samples): i * (1 / (samples - 1)), the last one 1.
    outX->assign(std::size_t(samples), 0.0);
    outY->assign(std::size_t(samples), 0.0);
    const double step = samples > 1 ? 1.0 / (samples - 1) : 0.0;
    int interval = 0;
    for (int s = 0; s < samples; ++s) {
        const double t = (s == samples - 1 && samples > 1) ? 1.0 : s * step;
        // The interval [u_k, u_{k+1}) holding t (the last one holds 1).
        while (interval < n - 1 && t >= u[interval + 1]) {
            ++interval;
        }
        const int k = interval;
        const double t0 = u[k];
        const double t1 = u[k + 1];
        const double hh = t1 - t0;
        const double da = t1 - t;
        const double db = t - t0;
        for (int d = 0; d < 2; ++d) {
            const double y0 = ys[d][k];
            const double y1 = ys[d][(k + 1) % n];
            const double m0 = ms[d][k];
            const double m1 = ms[d][(k + 1) % n];
            const double v = m0 * da * da * da / (6 * hh) + m1 * db * db * db / (6 * hh) +
                             (y0 / hh - m0 * hh / 6) * da + (y1 / hh - m1 * hh / 6) * db;
            (d == 0 ? *outX : *outY)[std::size_t(s)] = v;
        }
    }
}

Track generateTrack(const TrackParams& params) {
    PyRandom random;
    if (params.seed >= 0) {
        random.seed64(std::uint64_t(params.seed));
    } else {
        // seed = None in Python: the global generator was not reseeded.
        std::random_device device;
        random.seed64((std::uint64_t(device()) << 32) | device());
    }
    Track track;
    track.innerSeed = std::uint32_t(random.randint(0, 4294967295LL));
    random.seed64(track.innerSeed);

    const std::vector<Point2> checkpoints = trackCheckpoints(params, random);
    periodicSpline(checkpoints, params.checkpoints * params.resolution, &track.x, &track.y);
    std::reverse(track.x.begin(), track.x.end());
    std::reverse(track.y.begin(), track.y.end());
    return track;
}

}  // namespace tracelab::lf
