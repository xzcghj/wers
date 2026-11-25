#pragma once
// Building blocks of the reverb: delay lines, allpasses, filters and a small random generator.
// Plain C++ without JUCE. Nothing here allocates after allocate()/prepare().
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#define POG_HAS_SSE 1
#endif

namespace pog {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr float kPiF = 3.14159265358979f;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) noexcept { return 20.0f * std::log10(std::max(g, 1e-9f)); }

// Denormals would make the tail of a fading reverb many times slower. Flush them while we process.
class ScopedFlushDenormals {
public:
#ifdef POG_HAS_SSE
    ScopedFlushDenormals() noexcept : old_(_mm_getcsr()) { _mm_setcsr(old_ | 0x8040u); }
    ~ScopedFlushDenormals() { _mm_setcsr(old_); }
private:
    unsigned old_;
#endif
};

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) noexcept : s(seed * 2654435761u + 0x9e3779b9u) {
        if (s == 0) s = 1;
    }
    uint32_t next() noexcept {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float bipolar() noexcept { return static_cast<float>(next()) * (2.0f / 4294967296.0f) - 1.0f; }
    float unit() noexcept { return static_cast<float>(next()) * (1.0f / 4294967296.0f); }
};

inline int nextPow2(int n) noexcept {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// A ring buffer. read(k) is the sample written k writes ago, so read before write gives a delay of k.
class Delay {
public:
    void allocate(int maxDelay) {
        const int size = nextPow2(std::max(8, maxDelay + 8));
        buf_.assign(static_cast<size_t>(size), 0.0f);
        mask_ = size - 1;
        w_ = 0;
    }
    void clear() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    int capacity() const noexcept { return mask_ - 4; }
    void write(float x) noexcept {
        buf_[static_cast<size_t>(w_)] = x;
        w_ = (w_ + 1) & mask_;
    }
    float read(int k) const noexcept { return buf_[static_cast<size_t>((w_ - k) & mask_)]; }
    float readLinear(float d) const noexcept {
        const int i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        const float a = read(i), b = read(i + 1);
        return a + f * (b - a);
    }
    // Cubic Hermite between read(i) and read(i + 1). d must be at least 2.
    float readCubic(float d) const noexcept {
        const int i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        const float x0 = read(i - 1), x1 = read(i), x2 = read(i + 1), x3 = read(i + 2);
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * f + c2) * f + c1) * f + x1;
    }
private:
    std::vector<float> buf_;
    int mask_ = 0, w_ = 0;
};

// Schroeder allpass with an integer delay. Lossless, so it can sit inside a feedback loop.
class Allpass {
public:
    void allocate(int maxDelay) {
        buf_.assign(static_cast<size_t>(std::max(2, maxDelay + 1)), 0.0f);
        setDelay(std::min(delay_, maxDelay));
    }
    void setDelay(int d) noexcept {
        delay_ = std::clamp(d, 1, static_cast<int>(buf_.size()) - 1);
        if (i_ >= delay_) i_ = 0;
    }
    int delay() const noexcept { return delay_; }
    void setCoefficient(float g) noexcept { g_ = g; }
    void clear() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        i_ = 0;
    }
    float process(float x) noexcept {
        const float old = buf_[static_cast<size_t>(i_)];
        const float v = x - g_ * old;
        buf_[static_cast<size_t>(i_)] = v;
        if (++i_ >= delay_) i_ = 0;
        return old + g_ * v;
    }
private:
    std::vector<float> buf_ = std::vector<float>(2, 0.0f);
    int delay_ = 1, i_ = 0;
    float g_ = 0.5f;
};

// Topology-preserving one pole: lowpass out, highpass is the input minus it. Safe to retune every sample.
struct OnePole {
    float g = 0.5f, s = 0.0f;
    static float coefficient(float hz, float fs) noexcept {
        const float w = std::tan(kPiF * std::clamp(hz, 1.0f, 0.49f * fs) / fs);
        return w / (1.0f + w);
    }
    float lowpass(float x) noexcept {
        const float v = (x - s) * g;
        const float y = v + s;
        s = y + v;
        return y;
    }
    void clear() noexcept { s = 0.0f; }
};

// State variable filter (trapezoidal), 12 dB per octave, stable while its cutoff moves.
struct Svf {
    float a1 = 0, a2 = 0, a3 = 0, k = 1.4142f;
    float ic1 = 0, ic2 = 0;
    void set(float hz, float q, float fs) noexcept {
        const float g = std::tan(kPiF * std::clamp(hz, 5.0f, 0.49f * fs) / fs);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // Returns lowpass; band and high are written to the references.
    float tick(float x, float& band, float& high) noexcept {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        band = v1;
        high = x - k * v1 - v2;
        return v2;
    }
    float lowpass(float x) noexcept {
        float b, h;
        return tick(x, b, h);
    }
    float highpass(float x) noexcept {
        float b, h;
        tick(x, b, h);
        return h;
    }
    void clear() noexcept { ic1 = ic2 = 0.0f; }
};

// Biquad from the usual cookbook formulas, transposed direct form II. For fixed tone shaping.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Kind { Peak, LowShelf, HighShelf, LowPass, HighPass };
    void set(Kind kind, double hz, double q, double db, double fs) noexcept {
        hz = std::clamp(hz, 5.0, 0.48 * fs);
        const double a = std::pow(10.0, db / 40.0), w = 2.0 * kPi * hz / fs, c = std::cos(w), sn = std::sin(w), al = sn / (2.0 * q);
        double B0, B1, B2, A0, A1, A2;
        switch (kind) {
        case Peak:
            B0 = 1 + al * a; B1 = -2 * c; B2 = 1 - al * a; A0 = 1 + al / a; A1 = -2 * c; A2 = 1 - al / a;
            break;
        case LowShelf: {
            const double sq = 2.0 * std::sqrt(a) * al;
            B0 = a * ((a + 1) - (a - 1) * c + sq); B1 = 2 * a * ((a - 1) - (a + 1) * c); B2 = a * ((a + 1) - (a - 1) * c - sq);
            A0 = (a + 1) + (a - 1) * c + sq; A1 = -2 * ((a - 1) + (a + 1) * c); A2 = (a + 1) + (a - 1) * c - sq;
            break;
        }
        case HighShelf: {
            const double sq = 2.0 * std::sqrt(a) * al;
            B0 = a * ((a + 1) + (a - 1) * c + sq); B1 = -2 * a * ((a - 1) + (a + 1) * c); B2 = a * ((a + 1) + (a - 1) * c - sq);
            A0 = (a + 1) - (a - 1) * c + sq; A1 = 2 * ((a - 1) - (a + 1) * c); A2 = (a + 1) - (a - 1) * c - sq;
            break;
        }
        case LowPass:
            B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al;
            break;
        default:
            B0 = (1 + c) / 2; B1 = -(1 + c); B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al;
            break;
        }
        b0 = static_cast<float>(B0 / A0); b1 = static_cast<float>(B1 / A0); b2 = static_cast<float>(B2 / A0);
        a1 = static_cast<float>(A1 / A0); a2 = static_cast<float>(A2 / A0);
    }
    void bypass() noexcept {
        b0 = 1; b1 = b2 = a1 = a2 = 0;
    }
    float process(float x) noexcept {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void clear() noexcept { z1 = z2 = 0.0f; }
};

// Fast Walsh-Hadamard transform of 16 values, scaled to stay orthonormal: the mixing matrix of the network.
inline void hadamard16(float* x) noexcept {
    for (int h = 1; h < 16; h <<= 1)
        for (int i = 0; i < 16; i += h << 1)
            for (int j = i; j < i + h; ++j) {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    for (int i = 0; i < 16; ++i) x[i] *= 0.25f;
}

// Linear ramp across a block, for gains that must not step.
struct Ramp {
    float value = 0.0f, step = 0.0f;
    void to(float target, int samples) noexcept { step = (target - value) / static_cast<float>(std::max(1, samples)); }
    float next() noexcept {
        value += step;
        return value;
    }
    void snap(float v) noexcept {
        value = v;
        step = 0.0f;
    }
};

} // namespace pog
