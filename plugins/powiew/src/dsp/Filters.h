#pragma once
// Building blocks of the Powiew DSP: a state variable filter, a delay and a 2x/4x oversampler.
// Plain C++ without JUCE, so the tests can run them alone. Nothing allocates after prepare().
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace powiew {

inline constexpr double kPi = 3.14159265358979323846;

// Trapezoidal state variable filter. It stays stable and quiet while its coefficients move,
// which matters because every corner here follows a knob or a detector.
struct Svf {
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;
    float ic1 = 0.0f, ic2 = 0.0f;
    float v1 = 0.0f, v2 = 0.0f; // band and low output of the last tick

    // The prewarped corner. Kept below 0.45 of the rate, where the bilinear map still behaves.
    static float warp(double hz, double fs) noexcept { return static_cast<float>(std::tan(kPi * std::clamp(hz, 1.0, 0.45 * fs) / fs)); }
    void setG(float g, float q) noexcept {
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void set(double hz, double q, double fs) noexcept { setG(warp(hz, fs), static_cast<float>(q)); }
    void reset() noexcept { ic1 = ic2 = v1 = v2 = 0.0f; }

    inline void tick(float x) noexcept {
        const float v3 = x - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
    }
    inline float lowpass(float x) noexcept {
        tick(x);
        return v2;
    }
    inline float highpass(float x) noexcept {
        tick(x);
        return x - k * v1 - v2;
    }
};

// A whole number of samples of delay.
class Delay {
public:
    void prepare(int samples) {
        delay_ = std::max(0, samples);
        buffer_.assign(static_cast<size_t>(delay_) + 1, 0.0f);
        write_ = 0;
    }
    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    int samples() const noexcept { return delay_; }
    inline float process(float x) noexcept {
        if (delay_ == 0) return x;
        buffer_[static_cast<size_t>(write_)] = x;
        write_ = write_ == delay_ ? 0 : write_ + 1;
        return buffer_[static_cast<size_t>(write_)];
    }
private:
    std::vector<float> buffer_;
    int write_ = 0, delay_ = 0;
};

// A half-band low-pass from a Kaiser-windowed sinc, of length 4m + 3. Every other tap is zero apart from the
// centre (0.5), so only the even taps are kept. They are normalised to add up to 0.5: unity gain at DC.
inline std::vector<float> halfBandTaps(int length, double beta) {
    const int centre = (length - 1) / 2;
    auto bessel0 = [](double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k) {
            const double t = x / (2.0 * k);
            term *= t * t;
            sum += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    };
    std::vector<double> taps;
    double total = 0.0;
    for (int i = 0; i < length; i += 2) {
        const double t = static_cast<double>(i - centre);
        const double r = t / centre;
        const double window = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel0(beta);
        taps.push_back(std::sin(kPi * t * 0.5) / (kPi * t) * window);
        total += taps.back();
    }
    std::vector<float> out;
    for (double t : taps) out.push_back(static_cast<float>(t * 0.5 / total));
    return out;
}

// One sample in, two out. Zero-stuffing makes the odd output a plain delay of the input.
class HalfBandUp {
public:
    void prepare(const std::vector<float>& taps) {
        taps_ = taps;
        count_ = static_cast<int>(taps.size());
        history_.assign(static_cast<size_t>(count_) * 2, 0.0f);
        pos_ = 0;
    }
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        pos_ = 0;
    }
    inline void process(float x, float& even, float& odd) noexcept {
        pos_ = pos_ == 0 ? count_ - 1 : pos_ - 1;
        history_[static_cast<size_t>(pos_)] = x;
        history_[static_cast<size_t>(pos_ + count_)] = x;
        const float* h = history_.data() + pos_; // h[m] is the input m samples ago
        float sum = 0.0f;
        for (int m = 0; m < count_; ++m) sum += taps_[static_cast<size_t>(m)] * h[m];
        even = 2.0f * sum;
        odd = h[(count_ - 2) / 2];
    }
private:
    std::vector<float> taps_, history_;
    int count_ = 0, pos_ = 0;
};

// Two samples in, one out: filters, then keeps the even one.
class HalfBandDown {
public:
    void prepare(const std::vector<float>& taps) {
        taps_ = taps;
        count_ = static_cast<int>(taps.size());
        history_.assign(static_cast<size_t>(count_) * 2, 0.0f);
        odd_.assign(static_cast<size_t>(count_ / 2), 0.0f);
        pos_ = 0;
        oddPos_ = 0;
    }
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(odd_.begin(), odd_.end(), 0.0f);
        pos_ = 0;
        oddPos_ = 0;
    }
    inline float process(float even, float odd) noexcept {
        pos_ = pos_ == 0 ? count_ - 1 : pos_ - 1;
        history_[static_cast<size_t>(pos_)] = even;
        history_[static_cast<size_t>(pos_ + count_)] = even;
        const float* h = history_.data() + pos_;
        float sum = 0.0f;
        for (int m = 0; m < count_; ++m) sum += taps_[static_cast<size_t>(m)] * h[m];
        // The centre tap meets the odd sample from count/2 steps ago.
        auto& slot = odd_[static_cast<size_t>(oddPos_)];
        sum += 0.5f * slot;
        slot = odd;
        oddPos_ = oddPos_ + 1 == static_cast<int>(odd_.size()) ? 0 : oddPos_ + 1;
        return sum;
    }
private:
    std::vector<float> taps_, history_, odd_;
    int count_ = 0, pos_ = 0, oddPos_ = 0;
};

// Runs a nonlinearity at 2 or 4 times the rate so the harmonics it makes above the audio band are filtered
// away instead of folding back down as aliasing. The first stage is sharp (pass to 0.4535 of the rate, stop
// from 0.5465, about 100 dB down), the second only has to clear what the first leaves. The round trip is a
// whole number of samples, so the result lines up with a plain delay of the dry signal.
class Oversampler {
public:
    static constexpr int kLengthA = 139, kLengthB = 31;
    static constexpr double kBeta = 10.06; // Kaiser beta for about 100 dB

    void prepare(int factor) {
        factor_ = factor >= 4 ? 4 : (factor >= 2 ? 2 : 1);
        const auto a = halfBandTaps(kLengthA, kBeta), b = halfBandTaps(kLengthB, kBeta);
        upA_.prepare(a);
        downA_.prepare(a);
        upB_.prepare(b);
        downB_.prepare(b);
        reset();
    }
    void reset() noexcept {
        upA_.reset();
        downA_.reset();
        upB_.reset();
        downB_.reset();
        pad_ = 0.0f;
    }
    int factor() const noexcept { return factor_; }
    // Up and back down, in samples at the plain rate.
    int latency() const noexcept {
        const int a = (kLengthA - 1) / 2, b = (kLengthB - 1) / 2;
        if (factor_ == 4) return a + (b + 1) / 2;
        return factor_ == 2 ? a : 0;
    }
    inline void up(float x, float* out) noexcept {
        if (factor_ == 1) {
            out[0] = x;
        } else if (factor_ == 2) {
            upA_.process(x, out[0], out[1]);
        } else {
            float a0, a1;
            upA_.process(x, a0, a1);
            upB_.process(a0, out[0], out[1]);
            upB_.process(a1, out[2], out[3]);
        }
    }
    inline float down(const float* in) noexcept {
        if (factor_ == 1) return in[0];
        if (factor_ == 2) return downA_.process(in[0], in[1]);
        const float d0 = downB_.process(in[0], in[1]);
        const float d1 = downB_.process(in[2], in[3]);
        // One sample of delay at twice the rate makes the second stage's half-sample delay a whole one.
        const float out = downA_.process(pad_, d0);
        pad_ = d1;
        return out;
    }
private:
    HalfBandUp upA_, upB_;
    HalfBandDown downA_, downB_;
    int factor_ = 4;
    float pad_ = 0.0f;
};

// What the bell and the shelf add to the dry signal, as analog prototypes, for drawing the curve the engine
// applies. w is the frequency over the corner, already prewarped the way the filters are.
inline std::complex<double> bellDiff(double w, double peakGain, double q) {
    // (A^2 - 1) times the normalised band-pass k s / (s^2 + k s + 1).
    const std::complex<double> s(0.0, w);
    const double k = 1.0 / q;
    return (peakGain - 1.0) * k * s / (s * s + k * s + 1.0);
}

inline std::complex<double> shelfDiff(double w, double a, double q) {
    // High shelf A (A s^2 + sqrt(A)/Q s + 1) / (s^2 + sqrt(A)/Q s + A), minus the dry signal.
    const std::complex<double> s(0.0, w);
    const double sa = std::sqrt(a) / q;
    return a * (a * s * s + sa * s + 1.0) / (s * s + sa * s + a) - 1.0;
}

} // namespace powiew
