#include "sensorrng.h"

#include <cmath>

namespace tracelab::sensors {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
}

std::uint64_t hashLabel(const std::string& label) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : label) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t mixSeed(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

std::uint64_t deriveSeed(std::uint64_t master, const std::string& label) {
    return mixSeed(mixSeed(master) ^ hashLabel(label));
}

void SensorRng::reseed(std::uint64_t seed) {
    m_seed = seed;
    m_engine.seed(seed);
    m_hasSpare = false;
    m_spare = 0.0;
}

double SensorRng::uniform() {
    return double(m_engine() >> 11) * (1.0 / 9007199254740992.0);  // 2^-53
}

double SensorRng::gaussian() {
    if (m_hasSpare) {
        m_hasSpare = false;
        return m_spare;
    }
    const double u1 = 1.0 - uniform();  // (0, 1]: log() stays finite
    const double u2 = uniform();
    const double r = std::sqrt(-2.0 * std::log(u1));
    const double angle = kTwoPi * u2;
    m_spare = r * std::sin(angle);
    m_hasSpare = true;
    return r * std::cos(angle);
}

}  // namespace tracelab::sensors
