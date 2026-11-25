// The soft clip at the output, run at four times the sample rate so its new harmonics do not fold back as
// aliasing. The half-band filters are polyphase IIR: two chains of first-order allpasses whose sum is an
// elliptic low-pass. They delay the signal by only a sample or two, so there is no latency to report.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace doc {

// Allpass coefficients of a polyphase half-band low-pass with `count` coefficients and the given transition
// width (a fraction of the higher sample rate, centred on a quarter of it). The classic elliptic design.
inline void designHalfBand(double* coefs, int count, double transition) {
    const double pi = 3.14159265358979323846;
    double k = std::tan((1.0 - transition * 2.0) * pi / 4.0);
    k *= k;
    const double kk = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
    const double e2 = e * e, e4 = e2 * e2;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = count * 2 + 1;
    for (int index = 0; index < count; ++index) {
        const int c = index + 1;
        double num = 0.0, term = 0.0;
        int sign = 1;
        for (int i = 0; i < 64; ++i) {
            term = std::pow(q, static_cast<double>(i * (i + 1))) * std::sin((i * 2 + 1) * c * pi / order) * sign;
            num += term;
            sign = -sign;
            if (std::abs(term) < 1e-100) break;
        }
        double den = 0.0;
        sign = -1;
        for (int i = 1; i < 64; ++i) {
            term = std::pow(q, static_cast<double>(i * i)) * std::cos(i * 2 * c * pi / order) * sign;
            den += term;
            sign = -sign;
            if (std::abs(term) < 1e-100) break;
        }
        const double ww = num * std::pow(q, 0.25) / (den + 0.5);
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefs[index] = (1.0 - x) / (1.0 + x);
    }
}

// A chain of first-order allpasses, y = c (x - y1) + x1 each.
template <int N>
struct AllpassChain {
    std::array<float, N> c{}, x1{}, y1{};
    float run(float x) noexcept {
        for (int i = 0; i < N; ++i) {
            const float y = c[static_cast<size_t>(i)] * (x - y1[static_cast<size_t>(i)]) + x1[static_cast<size_t>(i)];
            x1[static_cast<size_t>(i)] = x;
            y1[static_cast<size_t>(i)] = y;
            x = y;
        }
        return x;
    }
    void reset() noexcept {
        x1.fill(0.0f);
        y1.fill(0.0f);
    }
};

// One half-band stage, either way: the even coefficients make one path and the odd ones the other.
template <int Coefs>
struct HalfBand {
    AllpassChain<Coefs / 2> even, odd;
    void set(const double* coefs) noexcept {
        for (int i = 0; i < Coefs / 2; ++i) {
            even.c[static_cast<size_t>(i)] = static_cast<float>(coefs[2 * i]);
            odd.c[static_cast<size_t>(i)] = static_cast<float>(coefs[2 * i + 1]);
        }
    }
    void up(float x, float& first, float& second) noexcept {
        first = even.run(x);
        second = odd.run(x);
    }
    float down(float first, float second) noexcept { return 0.5f * (even.run(second) + odd.run(first)); }
    void reset() noexcept {
        even.reset();
        odd.reset();
    }
};

// Untouched below about -1,5 dBFS, then a tanh shoulder that never quite reaches full scale.
inline float softClip(float x) noexcept {
    constexpr float knee = 0.84f, room = 1.0f - knee;
    const float a = std::abs(x);
    if (a <= knee) return x;
    return std::copysign(knee + room * std::tanh((a - knee) / room), x);
}

class Oversampler {
public:
    static constexpr int kOuterCoefs = 8, kInnerCoefs = 4;
    static constexpr double kOuterTransition = 0.04, kInnerTransition = 0.12;

    void prepare() noexcept {
        double outer[kOuterCoefs], inner[kInnerCoefs];
        designHalfBand(outer, kOuterCoefs, kOuterTransition);
        designHalfBand(inner, kInnerCoefs, kInnerTransition);
        upOuter_.set(outer);
        downOuter_.set(outer);
        upInner_.set(inner);
        downInner_.set(inner);
        reset();
    }
    void reset() noexcept {
        upOuter_.reset();
        downOuter_.reset();
        upInner_.reset();
        downInner_.reset();
    }
    // One sample in, clipped at four times the rate, one sample out. `peak` collects the loudest sample before the clip.
    float process(float x, float& peak) noexcept {
        float a, b;
        upOuter_.up(x, a, b);
        float s[4];
        upInner_.up(a, s[0], s[1]);
        upInner_.up(b, s[2], s[3]);
        for (float& v : s) {
            peak = std::max(peak, std::abs(v));
            v = softClip(v);
        }
        const float a2 = downInner_.down(s[0], s[1]);
        const float b2 = downInner_.down(s[2], s[3]);
        return downOuter_.down(a2, b2);
    }
    // The same path without the clip, for measuring what the filters alone do.
    float passThrough(float x) noexcept {
        float a, b, s[4];
        upOuter_.up(x, a, b);
        upInner_.up(a, s[0], s[1]);
        upInner_.up(b, s[2], s[3]);
        const float a2 = downInner_.down(s[0], s[1]);
        const float b2 = downInner_.down(s[2], s[3]);
        return downOuter_.down(a2, b2);
    }
private:
    HalfBand<kOuterCoefs> upOuter_, downOuter_;
    HalfBand<kInnerCoefs> upInner_, downInner_;
};

} // namespace doc
