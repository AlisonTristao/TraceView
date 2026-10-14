#ifndef TRACELAB_LF_TRACKPROGRESS_H
#define TRACELAB_LF_TRACKPROGRESS_H

// Line-follower benchmark: the pygame simulator's Cluster logic
// (graphics/graphics_elements.py, track_generator.generate_cluster) in
// geometry, without pixels: which track point is the next one to reach, the
// coverage, and the future points the controller gets.
//
// - The track points are split in 1 m cells exactly like generate_cluster():
//   point p goes to the first cell (i, j), i and j in [-L/2, L/2) in loop
//   order, with |p.x - i| < h and |p.y - j| < h, h = (L + W) / scale
//   (L = W = 100, scale = 300 px/m: h = 0.667 m, so the cells overlap).
// - Each step the cells within the render radius of the car's cell are
//   visited in Track.draw()'s order (offsets di, dj in [-r, r], di major,
//   di^2 + dj^2 < r^2); in each one the next point advances while it belongs
//   to that cell and lies in the reach area. The reach area
//   (Cluster._points_in_square) is, in the car frame (x lateral right, y
//   forward): -L_w < x < L_w and -L_w < y < 0, L_w = wheels distance -- a
//   2 L_w wide, L_w deep rectangle just behind the axle. A run of points
//   crossing into a cell visited earlier in the same step continues on the
//   next step, as in pygame.
// - Then Cluster.update(): future point j (1..N) is track point
//   next + j * spacing, in the car frame of the CURRENT pose, refreshed only
//   when that point exists and its cell is in the visited set (otherwise the
//   previous value is held, as the Python array is); the current point (the
//   one being reached) likewise, and its lateral coordinate is the lateral
//   error.
// The car frame here is the Python one: x lateral (positive right), y
// forward, metres.

#include <cstdint>
#include <vector>

#include "cardynamics.h"
#include "trackgenerator.h"

namespace tracelab::lf {

struct TrackProgressParams {
    double wheelsDistance = 0.15;  // m, the reach area's size
    int gridLength = 100;          // simulator.start_simulation(length=100)
    int gridWidth = 100;           // width=100
    double scale = 300.0;          // display "scale" (px/m): sets the cell overlap h
    double renderRadius = 4.0;     // display "render" (cells)
    int futureCount = 45;          // controller horizon N
    int futureSpacing = 3;         // track "future_spacing" (points)
};

// A point in the car frame: x lateral (right), y forward.
using CarFramePoint = Point2;

CarFramePoint worldToCar(const Point2& world, const Pose& pose);
Point2 carToWorld(const CarFramePoint& car, const Pose& pose);
// Cluster._points_in_square for a car-frame point.
bool inReachArea(const CarFramePoint& car, double wheelsDistance);

class TrackProgress {
public:
    TrackProgress(const std::vector<double>& x, const std::vector<double>& y,
                  const TrackProgressParams& params = {});

    const TrackProgressParams& params() const { return m_params; }
    void reset();

    // One frame: advances the next point and refreshes the future points
    // from `pose` (the pose after the step).
    void update(const Pose& pose);

    int pointCount() const { return int(m_x.size()); }
    int nextPoint() const { return m_next; }
    bool finished() const { return m_next >= pointCount(); }
    // Cluster._next_point / win * 100.
    double coverage() const;
    const std::vector<CarFramePoint>& futurePoints() const { return m_future; }
    CarFramePoint currentPoint() const { return m_current; }
    double lateralError() const { return m_current.x; }

    // The cell (row, col) generate_cluster() gives point `index`, -1 when none.
    int cellRow(int index) const { return m_cellRow[std::size_t(index)]; }
    int cellCol(int index) const { return m_cellCol[std::size_t(index)]; }

private:
    int cellKey(int row, int col) const { return row * m_params.gridWidth + col; }

    TrackProgressParams m_params;
    std::vector<double> m_x;
    std::vector<double> m_y;
    std::vector<int> m_cellRow;
    std::vector<int> m_cellCol;
    // Point indices of each cell, ascending.
    std::vector<std::vector<int>> m_cellPoints;
    // Track.draw()'s visit order: the (di, dj) offsets inside the radius.
    std::vector<std::pair<int, int>> m_offsets;
    std::vector<std::uint32_t> m_visitedStamp;
    std::uint32_t m_stamp = 0;

    int m_next = 0;
    std::vector<CarFramePoint> m_future;
    CarFramePoint m_current;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_TRACKPROGRESS_H
