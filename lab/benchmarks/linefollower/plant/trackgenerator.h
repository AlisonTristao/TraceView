#ifndef TRACELAB_LF_TRACKGENERATOR_H
#define TRACELAB_LF_TRACKGENERATOR_H

// Line-follower benchmark: the random track of line_follower_simulator
// (graphics/track_generator.py), ported so the same seed and parameters give
// the same points as the Python simulator:
//
//   random.seed(seed)                       # simulator.start_simulation
//   inner = random.randint(0, 2**32 - 1)    # generate_track
//   random.seed(inner)
//   checkpoints: radius * (cos t, sin t) or (cos t, sin t cos t), plus
//                uniform(noise/2, noise) (circle) / uniform(noise/3, noise)
//                (lemniscate) on x then y, noise = noise_level * radius
//   splprep([x, y], s=0, per=True); splev(linspace(0, 1, m * resolution))
//   reversed
//
// scipy's splprep with per=True overwrites the LAST checkpoint with the
// first one (it does not append it), parameterises by cumulative chord
// length normalised to [0, 1] and, with s = 0, FITPACK's clocur returns the
// periodic cubic spline with knots at the parameters: the C2 periodic cubic
// interpolant through the m - 1 distinct points. That is what is solved here
// (second-derivative form, one small dense system per coordinate).
//
// Units: metres, in the pygame simulator's track frame (x right, y down on
// screen), the same frame as the car pose and the runs/*_track.csv files.

#include <cstdint>
#include <vector>

namespace tracelab::lf {

enum class TrackType {
    Lemniscate = 0,  // graphics/track_generator.py LEMNISCATE
    Circle = 1,      // CIRCLE
};

// config.py section "track" (the generated shapes).
struct TrackParams {
    TrackType type = TrackType::Lemniscate;
    int checkpoints = 36;
    double trackRad = 30.0;     // m
    double noiseLevel = 0.225;  // fraction of the radius
    int resolution = 500;       // points per checkpoint
    // random.seed(seed) before generating; -1 = a random seed (Python: None).
    std::int64_t seed = 1112;
};

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct Track {
    std::vector<double> x;
    std::vector<double> y;
    // The seed generate_track() itself drew (randint(0, 2**32 - 1)).
    std::uint32_t innerSeed = 0;
    std::size_t size() const { return x.size(); }
};

// The checkpoints before smoothing (circle_/lemniscate_checkpoints), drawing
// from an already seeded generator.
class PyRandom;
std::vector<Point2> trackCheckpoints(const TrackParams& params, PyRandom& random);

// The periodic interpolating cubic spline of splprep(s=0, per=True) sampled
// at `samples` parameters linspace(0, 1), not reversed. `points` is the raw
// checkpoint list (its last point is replaced by the first, as scipy does).
void periodicSpline(const std::vector<Point2>& points, int samples, std::vector<double>* x,
                    std::vector<double>* y);

// The whole of generate_track() with simulator.py's seeding.
Track generateTrack(const TrackParams& params);

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_TRACKGENERATOR_H
