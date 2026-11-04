#pragma once
// Small shared helpers for the realtime DSP code. No allocations, no JUCE.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
 #define WERS_HAS_SSE 1
 #include <xmmintrin.h>
#else
 #define WERS_HAS_SSE 0
#endif

namespace wers {

inline constexpr double kPi = 3.14159265358979323846;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline double dbToGainD(double db) noexcept { return std::pow(10.0, db * 0.05); }
inline float powerToDb(double p, float floorDb = -140.0f) noexcept {
    return p > 1.0e-14 ? std::max(floorDb, static_cast<float>(10.0 * std::log10(p))) : floorDb;
}
inline float gainToDb(double g, float floorDb = -140.0f) noexcept {
    return g > 1.0e-7 ? std::max(floorDb, static_cast<float>(20.0 * std::log10(g))) : floorDb;
}
inline float clamp01(float x) noexcept { return std::clamp(x, 0.0f, 1.0f); }
inline float sigmoid(float x, float centre, float width) noexcept {
    const float t = (x - centre) / width;
    if (t > 12.0f) return 1.0f;
    if (t < -12.0f) return 0.0f;
    return 1.0f / (1.0f + std::exp(-t));
}
inline bool isFiniteFloat(float x) noexcept { return std::isfinite(x); }

// One-pole coefficient for a time constant in seconds at a given update rate.
inline double onePoleCoeff(double seconds, double rate) noexcept {
    return seconds <= 0.0 ? 1.0 : 1.0 - std::exp(-1.0 / (seconds * rate));
}

// Flushes denormals to zero for the scope. Safe to nest; restores the previous state.
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept {
#if WERS_HAS_SSE
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u); // FTZ | DAZ
#endif
    }
    ~ScopedFlushDenormals() {
#if WERS_HAS_SSE
        _mm_setcsr(saved_);
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
private:
#if WERS_HAS_SSE
    unsigned int saved_ = 0;
#endif
};

// Fixed-length delay. Writing and reading never allocate.
class DelayLine {
public:
    void prepare(int delaySamples) {
        delay_ = std::max(0, delaySamples);
        buf_.assign(static_cast<size_t>(delay_) + 1, 0.0f);
        pos_ = 0;
    }
    void reset() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }
    int delay() const noexcept { return delay_; }
    float process(float x) noexcept {
        if (delay_ == 0) return x;
        buf_[static_cast<size_t>(pos_)] = x;
        if (++pos_ > delay_) pos_ = 0;
        return buf_[static_cast<size_t>(pos_)];
    }
private:
    std::vector<float> buf_;
    int delay_ = 0, pos_ = 0;
};

} // namespace wers
