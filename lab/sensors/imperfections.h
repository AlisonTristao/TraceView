#ifndef TRACELAB_SENSORS_IMPERFECTIONS_H
#define TRACELAB_SENSORS_IMPERFECTIONS_H

// TraceLab sensors: the imperfections of one measured channel, discrete at
// the sensor block's sample period Ts (one apply() per tick). Every model is
// off in a default ImperfectionConfig, which passes the value through.
//
// apply(h) runs, in this order:
//   1. y = (1 + scale) h + bias                       scale factor, constant bias
//   2. y += b_rw,  b_rw[k] = b_rw[k-1] + sigma_rw sqrt(Ts) n   random-walk bias
//      (updated before use: after k ticks Var(b_rw) = k Ts sigma_rw^2)
//   3. y += b_gm,  b_gm[k+1] = e^(-Ts/tau) b_gm[k] + sigma sqrt(1 - e^(-2Ts/tau)) n
//      first-order Gauss-Markov, started from its stationary law N(0, sigma^2)
//   4. y += sigma_w n                                 white Gaussian noise
//   5. with probability p_out, y += +/- magnitude     outliers (random sign)
//   6. y = clamp(y, min, max)                          saturation
//   7. y = lsb round(y / lsb)                          quantization
//   8. y = value of d ticks ago                        delay (the oldest value
//                                                      until d ticks exist)
//   9. a new sample every N ticks (ticks 0, N, 2N, ...), held in between (ZOH)
//  10. a new sample is lost with probability p_drop:   dropout, the output
//      then holds the last one or is NaN (DropoutMode)
// NaN passes through every step.
//
// Each random process draws from its own stream (deriveSeed(seed, "white"),
// "random_walk", "gauss_markov", "outlier", "dropout") on every tick whether
// it is on or not, so switching one model on or off, or changing the rate,
// never shifts another model's noise. reset() restarts all of them.

#include <cstdint>
#include <deque>
#include <limits>

#include "sensorrng.h"

namespace tracelab::sensors {

enum class DropoutMode {
    HoldLast,  // the output keeps the last delivered sample
    NaN,       // the output is quiet NaN for that sample
};

struct ImperfectionConfig {
    double scale = 0.0;  // s in y = (1 + s) h + b
    double bias = 0.0;   // b, constant
    double whiteSigma = 0.0;
    double randomWalkSigma = 0.0;  // sigma_rw, units / sqrt(s)
    double randomWalkInitial = 0.0;
    double gaussMarkovSigma = 0.0;  // stationary standard deviation
    double gaussMarkovTau = 0.0;    // s, correlation time (<= 0: off)
    double outlierProbability = 0.0;
    double outlierMagnitude = 0.0;
    double saturationMin = -std::numeric_limits<double>::infinity();
    double saturationMax = std::numeric_limits<double>::infinity();
    double quantization = 0.0;  // LSB (<= 0: off)
    int delaySamples = 0;       // d
    int rateDivider = 1;        // N: a new sample every N ticks
    double dropoutProbability = 0.0;
    DropoutMode dropoutMode = DropoutMode::HoldLast;
};

class ChannelImperfections {
public:
    // std::invalid_argument for ts <= 0, delaySamples < 0, rateDivider < 1,
    // a probability outside [0, 1], saturationMin > saturationMax or a
    // negative sigma.
    explicit ChannelImperfections(const ImperfectionConfig& config = {}, double ts = 1.0,
                                  std::uint64_t seed = 0);

    const ImperfectionConfig& config() const {
        return m_config;
    }
    double ts() const {
        return m_ts;
    }
    std::uint64_t seed() const {
        return m_seed;
    }

    // One tick: the measured value for the true value h.
    double apply(double h);
    // Back to tick 0 with the same streams.
    void reset();

    long long tick() const {
        return m_tick;
    }
    double randomWalkBias() const {
        return m_randomWalk;
    }
    double gaussMarkovBias() const {
        return m_gaussMarkov;
    }
    // Whether the last apply() was a sample tick that dropped out.
    bool lastDropped() const {
        return m_lastDropped;
    }

private:
    ImperfectionConfig m_config;
    double m_ts;
    std::uint64_t m_seed;
    double m_gmDecay = 0.0;
    double m_gmDrive = 0.0;
    SensorRng m_white;
    SensorRng m_walk;
    SensorRng m_markov;
    SensorRng m_outlier;
    SensorRng m_dropout;
    double m_randomWalk = 0.0;
    double m_gaussMarkov = 0.0;
    std::deque<double> m_delay;
    double m_output = std::numeric_limits<double>::quiet_NaN();
    bool m_lastDropped = false;
    long long m_tick = 0;
};

}  // namespace tracelab::sensors

#endif  // TRACELAB_SENSORS_IMPERFECTIONS_H
