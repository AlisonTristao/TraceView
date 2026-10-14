#include "imperfections.h"

#include <cmath>
#include <stdexcept>

namespace tracelab::sensors {

namespace {

bool isProbability(double p) {
    return p >= 0.0 && p <= 1.0;
}

}  // namespace

ChannelImperfections::ChannelImperfections(const ImperfectionConfig& config, double ts,
                                           std::uint64_t seed)
    : m_config(config), m_ts(ts), m_seed(seed) {
    if (!(ts > 0.0)) {
        throw std::invalid_argument("ChannelImperfections: ts must be > 0");
    }
    if (config.delaySamples < 0 || config.rateDivider < 1) {
        throw std::invalid_argument("ChannelImperfections: delay >= 0 and rate divider >= 1");
    }
    if (!isProbability(config.outlierProbability) || !isProbability(config.dropoutProbability)) {
        throw std::invalid_argument("ChannelImperfections: probabilities must be in [0, 1]");
    }
    if (config.saturationMin > config.saturationMax) {
        throw std::invalid_argument("ChannelImperfections: saturation min > max");
    }
    if (config.whiteSigma < 0.0 || config.randomWalkSigma < 0.0 || config.gaussMarkovSigma < 0.0) {
        throw std::invalid_argument("ChannelImperfections: negative sigma");
    }
    if (config.gaussMarkovTau > 0.0) {
        m_gmDecay = std::exp(-ts / config.gaussMarkovTau);
        m_gmDrive = config.gaussMarkovSigma * std::sqrt(1.0 - m_gmDecay * m_gmDecay);
    }
    reset();
}

void ChannelImperfections::reset() {
    m_white.reseed(deriveSeed(m_seed, "white"));
    m_walk.reseed(deriveSeed(m_seed, "random_walk"));
    m_markov.reseed(deriveSeed(m_seed, "gauss_markov"));
    m_outlier.reseed(deriveSeed(m_seed, "outlier"));
    m_dropout.reseed(deriveSeed(m_seed, "dropout"));
    m_randomWalk = m_config.randomWalkInitial;
    // Stationary start: the first draw of the Gauss-Markov stream.
    const double start = m_markov.gaussian();
    m_gaussMarkov = m_config.gaussMarkovTau > 0.0 ? m_config.gaussMarkovSigma * start : 0.0;
    m_delay.clear();
    m_output = std::numeric_limits<double>::quiet_NaN();
    m_lastDropped = false;
    m_tick = 0;
}

double ChannelImperfections::apply(double h) {
    const ImperfectionConfig& c = m_config;
    // Every stream draws on every tick (see the header).
    const double nWalk = m_walk.gaussian();
    const double nMarkov = m_markov.gaussian();
    const double nWhite = m_white.gaussian();
    const double outlierDraw = m_outlier.uniform();
    const double outlierSign = m_outlier.uniform() < 0.5 ? -1.0 : 1.0;
    const double dropoutDraw = m_dropout.uniform();

    double y = (1.0 + c.scale) * h + c.bias;

    m_randomWalk += c.randomWalkSigma * std::sqrt(m_ts) * nWalk;
    y += m_randomWalk;

    if (c.gaussMarkovTau > 0.0) {
        y += m_gaussMarkov;
        m_gaussMarkov = m_gmDecay * m_gaussMarkov + m_gmDrive * nMarkov;
    }

    y += c.whiteSigma * nWhite;

    if (outlierDraw < c.outlierProbability) {
        y += outlierSign * c.outlierMagnitude;
    }

    if (y < c.saturationMin) {
        y = c.saturationMin;
    } else if (y > c.saturationMax) {
        y = c.saturationMax;
    }

    if (c.quantization > 0.0 && std::isfinite(y)) {
        y = c.quantization * std::round(y / c.quantization);
    }

    if (c.delaySamples > 0) {
        m_delay.push_back(y);
        if (int(m_delay.size()) > c.delaySamples + 1) {
            m_delay.pop_front();
        }
        y = m_delay.front();
    }

    m_lastDropped = false;
    if (m_tick % c.rateDivider == 0) {
        if (dropoutDraw < c.dropoutProbability) {
            m_lastDropped = true;
            if (c.dropoutMode == DropoutMode::NaN) {
                m_output = std::numeric_limits<double>::quiet_NaN();
            }
            // HoldLast: m_output stays (NaN before the first sample).
        } else {
            m_output = y;
        }
    }
    ++m_tick;
    return m_output;
}

}  // namespace tracelab::sensors
