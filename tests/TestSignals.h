#pragma once
#include "dsp/Filters.h"
#include <cstdio>
#include <vector>

namespace wers::test {

inline std::vector<float> sine(double f, double db, int n, double fs, double phase = 0.0) {
    std::vector<float> x(static_cast<size_t>(n));
    const double a = dbToGainD(db);
    for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(a * std::sin(2.0 * kPi * f * i / fs + phase));
    return x;
}

class Lcg {
public:
    explicit Lcg(uint32_t seed = 1234567u) : s_(seed) {}
    float next() { s_ = s_ * 1664525u + 1013904223u; return static_cast<float>(static_cast<int32_t>(s_)) / 2147483648.0f; }
private:
    uint32_t s_;
};

inline std::vector<float> noise(double db, int n, uint32_t seed = 99u) {
    std::vector<float> x(static_cast<size_t>(n));
    Lcg r(seed);
    const float a = static_cast<float>(dbToGainD(db)) * 1.732f;
    for (auto& v : x) v = a * r.next();
    return x;
}

// Noise filtered to a band, normalised to an RMS level.
inline std::vector<float> bandNoise(double lo, double hi, double db, int n, double fs, uint32_t seed = 7u) {
    auto x = noise(0.0, n, seed);
    Svf hp, lp;
    hp.set(lo, 0.7, fs); lp.set(hi, 0.7, fs);
    Svf hp2, lp2;
    hp2.set(lo, 0.7, fs); lp2.set(hi, 0.7, fs);
    double e = 0;
    for (auto& v : x) {
        double y = hp.tick(v).hp; y = hp2.tick(y).hp; y = lp.tick(y).lp; y = lp2.tick(y).lp;
        v = static_cast<float>(y);
        e += y * y;
    }
    const double g = dbToGainD(db) / std::sqrt(e / n + 1e-20);
    for (auto& v : x) v = static_cast<float>(v * g);
    return x;
}

// Voiced sound with falling harmonics and two formant-like bumps, normalised to an RMS level.
inline std::vector<float> vowel(double f0, double db, int n, double fs) {
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    const double f1 = 700, f2 = 1200;
    for (int h = 1; h * f0 < std::min(6000.0, fs * 0.45); ++h) {
        const double f = h * f0;
        const double amp = 1.0 / (1.0 + std::pow(f / 500.0, 1.6))
            * (1.0 + 2.5 / (1.0 + std::pow((f - f1) / 120.0, 2.0)) + 1.5 / (1.0 + std::pow((f - f2) / 150.0, 2.0)));
        for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] += static_cast<float>(amp * std::sin(2.0 * kPi * f * i / fs + h * 0.7));
    }
    double e = 0; for (auto v : x) e += static_cast<double>(v) * v;
    const double g = dbToGainD(db) / std::sqrt(e / n + 1e-20);
    for (auto& v : x) v = static_cast<float>(v * g);
    return x;
}

inline double rmsDb(const float* x, int n) {
    double e = 0; for (int i = 0; i < n; ++i) e += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(e / std::max(1, n) + 1e-30);
}
inline double rmsDb(const std::vector<float>& x, int from = 0, int to = -1) {
    if (to < 0) to = static_cast<int>(x.size());
    return rmsDb(x.data() + from, to - from);
}

// Level of a sine component measured by correlation, in dB.
inline double toneDb(const float* x, int n, double f, double fs) {
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) { re += x[i] * std::cos(2 * kPi * f * i / fs); im += x[i] * std::sin(2 * kPi * f * i / fs); }
    return 20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / n + 1e-20);
}

inline void append(std::vector<float>& a, const std::vector<float>& b) { a.insert(a.end(), b.begin(), b.end()); }

// Runs a mono buffer through a stereo-capable processor in blocks of a chosen size.
template <typename Fn>
void inBlocks(std::vector<float>& x, int block, Fn&& fn) {
    for (size_t pos = 0; pos < x.size(); pos += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), x.size() - pos));
        float* ch[1] = { x.data() + pos };
        fn(ch, n, static_cast<int64_t>(pos));
    }
}

} // namespace wers::test
