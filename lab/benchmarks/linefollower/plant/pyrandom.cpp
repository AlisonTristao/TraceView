#include "pyrandom.h"

// Modules/_randommodule.c of CPython (MT19937 by Matsumoto and Nishimura).

namespace tracelab::lf {

namespace {
constexpr int M = 397;
constexpr std::uint32_t kMatrixA = 0x9908b0dfU;
constexpr std::uint32_t kUpperMask = 0x80000000U;
constexpr std::uint32_t kLowerMask = 0x7fffffffU;
}  // namespace

void PyRandom::initGenrand(std::uint32_t s) {
    m_mt[0] = s;
    for (int i = 1; i < N; ++i) {
        m_mt[i] = 1812433253U * (m_mt[i - 1] ^ (m_mt[i - 1] >> 30)) + std::uint32_t(i);
    }
    m_index = N;
}

void PyRandom::initByArray(const std::uint32_t* key, int length) {
    initGenrand(19650218U);
    int i = 1;
    int j = 0;
    for (int k = (N > length ? N : length); k; --k) {
        m_mt[i] = (m_mt[i] ^ ((m_mt[i - 1] ^ (m_mt[i - 1] >> 30)) * 1664525U)) + key[j] +
                  std::uint32_t(j);
        ++i;
        ++j;
        if (i >= N) {
            m_mt[0] = m_mt[N - 1];
            i = 1;
        }
        if (j >= length) {
            j = 0;
        }
    }
    for (int k = N - 1; k; --k) {
        m_mt[i] = (m_mt[i] ^ ((m_mt[i - 1] ^ (m_mt[i - 1] >> 30)) * 1566083941U)) - std::uint32_t(i);
        ++i;
        if (i >= N) {
            m_mt[0] = m_mt[N - 1];
            i = 1;
        }
    }
    m_mt[0] = 0x80000000U;
    m_index = N;
}

void PyRandom::seed64(std::uint64_t n) {
    const std::uint32_t key[2] = {std::uint32_t(n & 0xffffffffU), std::uint32_t(n >> 32)};
    initByArray(key, key[1] != 0 ? 2 : 1);
}

std::uint32_t PyRandom::genrandUint32() {
    static constexpr std::uint32_t mag01[2] = {0x0U, kMatrixA};
    std::uint32_t y;
    if (m_index >= N) {
        int kk = 0;
        for (; kk < N - M; ++kk) {
            y = (m_mt[kk] & kUpperMask) | (m_mt[kk + 1] & kLowerMask);
            m_mt[kk] = m_mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (; kk < N - 1; ++kk) {
            y = (m_mt[kk] & kUpperMask) | (m_mt[kk + 1] & kLowerMask);
            m_mt[kk] = m_mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        y = (m_mt[N - 1] & kUpperMask) | (m_mt[0] & kLowerMask);
        m_mt[N - 1] = m_mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
        m_index = 0;
    }
    y = m_mt[m_index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

std::uint64_t PyRandom::getrandbits(int k) {
    if (k <= 0) {
        return 0;
    }
    if (k <= 32) {
        return genrandUint32() >> (32 - k);
    }
    // Words from the least significant up; the last one drops its low bits.
    const std::uint64_t low = genrandUint32();
    const std::uint64_t high = genrandUint32() >> (64 - k);
    return low | (high << 32);
}

double PyRandom::random() {
    const std::uint32_t a = genrandUint32() >> 5;
    const std::uint32_t b = genrandUint32() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

double PyRandom::uniform(double a, double b) {
    return a + (b - a) * random();
}

std::uint64_t PyRandom::randbelow(std::uint64_t n) {
    if (n == 0) {
        return 0;
    }
    int k = 0;
    for (std::uint64_t v = n; v; v >>= 1) {
        ++k;  // n.bit_length()
    }
    std::uint64_t r = getrandbits(k);
    while (r >= n) {
        r = getrandbits(k);
    }
    return r;
}

std::int64_t PyRandom::randint(std::int64_t a, std::int64_t b) {
    return a + std::int64_t(randbelow(std::uint64_t(b - a) + 1U));
}

}  // namespace tracelab::lf
