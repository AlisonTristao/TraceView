#include "trackprogress.h"

#include <algorithm>
#include <cmath>

namespace tracelab::lf {

CarFramePoint worldToCar(const Point2& world, const Pose& pose) {
    const double dx = world.x - pose.x;
    const double dy = world.y - pose.y;
    const double c = std::cos(pose.theta);
    const double s = std::sin(pose.theta);
    // Right side (-sin, cos), forward (cos, sin).
    return {-dx * s + dy * c, dx * c + dy * s};
}

Point2 carToWorld(const CarFramePoint& car, const Pose& pose) {
    const double c = std::cos(pose.theta);
    const double s = std::sin(pose.theta);
    return {pose.x - car.x * s + car.y * c, pose.y + car.x * c + car.y * s};
}

bool inReachArea(const CarFramePoint& car, double wheelsDistance) {
    // Screen: x0 - d < x1 < x0 + d and y0 < y1 < y0 + d (y down), d = L_w px.
    return car.x > -wheelsDistance && car.x < wheelsDistance && car.y < 0.0 &&
           car.y > -wheelsDistance;
}

TrackProgress::TrackProgress(const std::vector<double>& x, const std::vector<double>& y,
                             const TrackProgressParams& params)
    : m_params(params), m_x(x), m_y(y) {
    const int count = int(std::min(m_x.size(), m_y.size()));
    m_x.resize(std::size_t(count));
    m_y.resize(std::size_t(count));

    // generate_cluster(): the first cell in loop order whose square holds the
    // point. The x and y tests are independent, so that is the smallest i
    // passing the x test, then the smallest j passing the y test.
    const int halfL = params.gridLength / 2;  // -length // 2 for even sizes
    const int halfW = params.gridWidth / 2;
    const double h = double(params.gridLength + params.gridWidth) / params.scale;
    const auto firstCell = [h](double v, int lo, int hi) {
        int i = int(std::floor(v - h)) - 1;
        i = std::max(i, lo);
        for (; i < hi; ++i) {
            const double a = double(i);
            if (v > a - h && v < a + h) {
                return i;
            }
            if (a - h >= v) {
                break;
            }
        }
        return hi;  // none
    };
    m_cellRow.assign(std::size_t(count), -1);
    m_cellCol.assign(std::size_t(count), -1);
    m_cellPoints.assign(std::size_t(params.gridLength) * params.gridWidth, {});
    for (int k = 0; k < count; ++k) {
        const int i = firstCell(m_x[std::size_t(k)], -halfL, params.gridLength - halfL);
        const int j = firstCell(m_y[std::size_t(k)], -halfW, params.gridWidth - halfW);
        if (i >= params.gridLength - halfL || j >= params.gridWidth - halfW) {
            continue;  // outside the grid: never reachable, as in pygame
        }
        const int row = i + halfL;
        const int col = j + halfW;
        m_cellRow[std::size_t(k)] = row;
        m_cellCol[std::size_t(k)] = col;
        m_cellPoints[std::size_t(cellKey(row, col))].push_back(k);
    }

    const double visible = params.renderRadius;
    const int r = int(std::ceil(visible));
    for (int di = -r; di <= r; ++di) {
        for (int dj = -r; dj <= r; ++dj) {
            if (di * di + dj * dj < visible * visible) {
                m_offsets.emplace_back(di, dj);
            }
        }
    }
    m_visitedStamp.assign(m_cellPoints.size(), 0);
    reset();
}

void TrackProgress::reset() {
    m_next = 0;
    m_future.assign(std::size_t(std::max(m_params.futureCount, 0)), CarFramePoint{});
    m_current = {};
}

double TrackProgress::coverage() const {
    return m_x.empty() ? 0.0 : double(m_next) / double(m_x.size()) * 100.0;
}

void TrackProgress::update(const Pose& pose) {
    ++m_stamp;
    const int rows = m_params.gridLength;
    const int cols = m_params.gridWidth;
    const int row0 = int(std::floor(pose.x + m_params.gridLength / 2));
    const int col0 = int(std::floor(pose.y + m_params.gridWidth / 2));
    const int count = pointCount();

    // Track.draw(): every visible cell, in order, advances the next point
    // while it is one of its own and lies in the reach area.
    for (const auto& [di, dj] : m_offsets) {
        const int row = row0 + di;
        const int col = col0 + dj;
        if (row < 0 || row >= rows || col < 0 || col >= cols) {
            continue;
        }
        const int key = cellKey(row, col);
        m_visitedStamp[std::size_t(key)] = m_stamp;
        const std::vector<int>& points = m_cellPoints[std::size_t(key)];
        if (points.empty()) {
            continue;
        }
        auto it = std::lower_bound(points.begin(), points.end(), m_next);
        while (it != points.end() && *it == m_next) {
            const Point2 world{m_x[std::size_t(*it)], m_y[std::size_t(*it)]};
            if (!inReachArea(worldToCar(world, pose), m_params.wheelsDistance)) {
                break;
            }
            ++m_next;
            ++it;
        }
    }

    // Cluster.update(): the points of the visited cells refresh the future
    // points and the current point; the others keep their old values.
    const auto visited = [&](int index) {
        const int row = m_cellRow[std::size_t(index)];
        return row >= 0 && m_visitedStamp[std::size_t(cellKey(row, m_cellCol[std::size_t(index)]))] ==
                               m_stamp;
    };
    const int spacing = std::max(m_params.futureSpacing, 1);
    for (int j = 1; j <= int(m_future.size()); ++j) {
        const long long index = (long long)m_next + (long long)j * spacing;
        if (index >= count || !visited(int(index))) {
            continue;
        }
        m_future[std::size_t(j - 1)] =
            worldToCar({m_x[std::size_t(index)], m_y[std::size_t(index)]}, pose);
    }
    if (m_next < count && visited(m_next)) {
        m_current = worldToCar({m_x[std::size_t(m_next)], m_y[std::size_t(m_next)]}, pose);
    }
}

}  // namespace tracelab::lf
