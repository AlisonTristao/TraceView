#ifndef TRACELAB_LF_PYRANDOM_H
#define TRACELAB_LF_PYRANDOM_H

// CPython's `random` module, bit for bit, for the calls the line-follower
// track generator makes: MT19937 seeded from an int through init_by_array
// (random.seed(n)), random(), uniform(a, b), getrandbits(k) and randint(a, b)
// (randrange through _randbelow with getrandbits). The same seed gives the
// same numbers as Python 3.x, so a C++ track equals the Python one.

#include <array>
#include <cstdint>

namespace tracelab::lf {

class PyRandom {
public:
    explicit PyRandom(std::uint64_t seed = 0) { seed64(seed); }

    // random.seed(n) for a non-negative int n < 2**64 (the key is n split in
    // 32-bit words, least significant first; 0 gives the key [0]).
    void seed64(std::uint64_t n);

    std::uint32_t genrandUint32();
    // getrandbits(k), 1 <= k <= 64.
    std::uint64_t getrandbits(int k);
    // random.random(): 53-bit float in [0, 1).
    double random();
    // random.uniform(a, b) = a + (b - a) * random().
    double uniform(double a, double b);
    // random.randint(a, b), b - a + 1 <= 2**63.
    std::int64_t randint(std::int64_t a, std::int64_t b);

private:
    void initGenrand(std::uint32_t s);
    void initByArray(const std::uint32_t* key, int length);
    std::uint64_t randbelow(std::uint64_t n);

    static constexpr int N = 624;
    std::array<std::uint32_t, N> m_mt{};
    int m_index = N + 1;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_PYRANDOM_H
