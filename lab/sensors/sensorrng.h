#ifndef TRACELAB_SENSORS_SENSORRNG_H
#define TRACELAB_SENSORS_SENSORRNG_H

// TraceLab sensors: the reproducible random numbers behind the synthetic
// imperfections (simulation side only, never exported to firmware).
//
// - The engine is std::mt19937_64, whose output sequence the C++ standard
//   fixes (its 10000th value is 9981545732273789042), so the same seed gives
//   the same raw numbers with every standard library.
// - The distributions are this file's own: uniform() takes the top 53 bits
//   of one draw, gaussian() is Box-Muller over two uniforms (the second
//   value of each pair is kept for the next call). std::uniform_real_
//   distribution and std::normal_distribution are implementation-defined and
//   differ between libstdc++, libc++ and MSVC; std::log/cos/sin may still
//   differ in the last bit between C libraries.
// - Streams: deriveSeed(master, label) mixes a label (FNV-1a) into the
//   master seed (SplitMix64 finaliser). A sensor's channels and the
//   processes inside a channel each get their own stream named after them,
//   so adding a sensor, a channel or turning an imperfection on never
//   changes another one's noise. The sensor master seed is independent of
//   the track seed.

#include <cstdint>
#include <random>
#include <string>

namespace tracelab::sensors {

// FNV-1a 64 of `label`.
std::uint64_t hashLabel(const std::string& label);
// SplitMix64's finaliser.
std::uint64_t mixSeed(std::uint64_t value);
// The seed of the stream `label` under `master`.
std::uint64_t deriveSeed(std::uint64_t master, const std::string& label);

class SensorRng {
public:
    explicit SensorRng(std::uint64_t seed = 0) {
        reseed(seed);
    }

    void reseed(std::uint64_t seed);
    std::uint64_t seed() const {
        return m_seed;
    }

    std::uint64_t nextU64() {
        return m_engine();
    }
    // [0, 1), 53-bit resolution.
    double uniform();
    // Standard normal (mean 0, variance 1), Box-Muller.
    double gaussian();

private:
    std::mt19937_64 m_engine;
    std::uint64_t m_seed = 0;
    double m_spare = 0.0;
    bool m_hasSpare = false;
};

}  // namespace tracelab::sensors

#endif  // TRACELAB_SENSORS_SENSORRNG_H
