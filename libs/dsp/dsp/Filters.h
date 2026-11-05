#pragma once
// Filter primitives. Svf is a Zavalishin TPT state-variable filter, which stays stable under
// per-sample modulation. Biquad is used where a static, exactly known response is needed.
#include "Common.h"

namespace wers {

class Svf {
public:
    struct Out { double lp, bp, hp; };

    void set(double fc, double q, double fs) noexcept {
        g_ = std::tan(kPi * std::min(fc, 0.49 * fs) / fs);
        k_ = 1.0 / q;
        a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }
    void reset() noexcept { ic1_ = ic2_ = 0.0; }
    Out tick(double x) noexcept {
        const double v3 = x - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return { v2, v1, x - k_ * v1 - v2 };
    }
    // Unity-gain-at-centre band output.
    double band(double x) noexcept { return k_ * tick(x).bp; }
    double k() const noexcept { return k_; }
private:
    double g_ = 0.1, k_ = 1.0, a1_ = 0.0, a2_ = 0.0, a3_ = 0.0, ic1_ = 0.0, ic2_ = 0.0;
};

// Magnitude of `1 + (gain - 1) * band` for an Svf bell at frequency f. Used to predict in the
// analysis model exactly what the realtime bell filters do.
inline double svfBellMagnitude(double f, double fc, double q, double gain, double fs) noexcept {
    const double w = std::tan(kPi * std::min(f, 0.49 * fs) / fs) / std::tan(kPi * std::min(fc, 0.49 * fs) / fs);
    const double re = 1.0 - w * w, im = w / q;
    const double den = re * re + im * im;
    // band = (j w / q) / (re + j w / q)
    const double bRe = (im * im) / den, bIm = (im * re) / den;
    const double hRe = 1.0 + (gain - 1.0) * bRe, hIm = (gain - 1.0) * bIm;
    return std::sqrt(hRe * hRe + hIm * hIm);
}

class Biquad {
public:
    enum class Type { LowPass, HighPass, Peak, LowShelf, HighShelf };

    void reset() noexcept { z1_ = z2_ = 0.0; }
    double tick(double x) noexcept {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }
    void set(Type type, double f, double q, double gainDb, double fs) noexcept {
        const double w = 2.0 * kPi * std::min(f, 0.49 * fs) / fs;
        const double cw = std::cos(w), sw = std::sin(w), alpha = sw / (2.0 * q);
        const double A = std::pow(10.0, gainDb / 40.0);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type) {
            case Type::LowPass:  b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::HighPass: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::Peak:     b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Type::LowShelf: {
                const double s = 2.0 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::HighShelf: {
                const double s = 2.0 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }
    double magnitude(double f, double fs) const noexcept {
        const double w = 2.0 * kPi * f / fs, c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0_ + b1_ * c1 + b2_ * c2, ni = -(b1_ * s1 + b2_ * s2);
        const double dr = 1.0 + a1_ * c1 + a2_ * c2, di = -(a1_ * s1 + a2_ * s2);
        return std::sqrt((nr * nr + ni * ni) / std::max(1e-30, dr * dr + di * di));
    }
    void setRaw(double b0, double b1, double b2, double a1, double a2) noexcept {
        b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
    }
private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

// Linear-phase FIR halfband pair for 2x oversampling. Round trip latency is exact and integer.
class Halfband2x {
public:
    static constexpr int kTaps = 47;
    static constexpr int kLatency = (kTaps - 1) / 2; // base-rate samples, round trip

    Halfband2x() noexcept {
        const int m = (kTaps - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            const int n = i - m;
            const double x = 0.5 * n;
            const double sinc = n == 0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double t = static_cast<double>(n) / (m + 1);
            const double win = 0.42 + 0.5 * std::cos(kPi * t) + 0.08 * std::cos(2.0 * kPi * t);
            c_[static_cast<size_t>(i)] = static_cast<float>(0.5 * sinc * win);
            sum += c_[static_cast<size_t>(i)];
        }
        for (auto& v : c_) v = static_cast<float>(v / sum);
        reset();
    }
    void reset() noexcept { up_.fill(0.0f); down_.fill(0.0f); upPos_ = downPos_ = 0; }

    void up(float x, float& a, float& b) noexcept {
        a = pushAndConvolve(up_, upPos_, x) * 2.0f;
        b = pushAndConvolve(up_, upPos_, 0.0f) * 2.0f;
    }
    float down(float a, float b) noexcept {
        const float out = pushAndConvolve(down_, downPos_, a); // even samples line up with the input grid
        down_[static_cast<size_t>(downPos_)] = b;
        if (++downPos_ >= kTaps) downPos_ = 0;
        return out;
    }
private:
    using Hist = std::array<float, kTaps>;
    float pushAndConvolve(Hist& h, int& pos, float x) noexcept {
        h[static_cast<size_t>(pos)] = x;
        float acc = 0.0f;
        int idx = pos;
        for (int i = 0; i < kTaps; ++i) {
            acc += c_[static_cast<size_t>(i)] * h[static_cast<size_t>(idx)];
            if (--idx < 0) idx = kTaps - 1;
        }
        if (++pos >= kTaps) pos = 0;
        return acc;
    }
    std::array<float, kTaps> c_{};
    Hist up_{}, down_{};
    int upPos_ = 0, downPos_ = 0;
};

// The ten bell bands used by Miejsce and by the word fixes in the beat.
inline constexpr int kCutBands = 10;
inline constexpr std::array<double, kCutBands> kCutCentres{ 250, 400, 630, 1000, 1500, 2200, 3200, 4600, 6800, 10000 };
inline constexpr double kCutQ = 1.3;
// The four presence bells used for vocal word fixes.
inline constexpr int kBoostBands = 4;
inline constexpr std::array<double, kBoostBands> kBoostCentres{ 2500, 3500, 5000, 7000 };
inline constexpr double kBoostQ = 1.2;

// Tracks band power in the ten bands of Miejsce, at a fixed hop counted in samples.
class BandEnvelope {
public:
    void prepare(double fs, double attackSec = 0.003, double releaseSec = 0.04) {
        fs_ = fs;
        for (int i = 0; i < kCutBands; ++i) { bank_[static_cast<size_t>(i)].set(kCutCentres[static_cast<size_t>(i)], kCutQ, fs); }
        att_ = onePoleCoeff(attackSec, fs);
        rel_ = onePoleCoeff(releaseSec, fs);
        reset();
    }
    void reset() noexcept {
        for (auto& s : bank_) s.reset();
        power_.fill(0.0);
    }
    void tick(double x) noexcept {
        for (size_t i = 0; i < kCutBands; ++i) {
            const double b = bank_[i].band(x);
            const double p = b * b;
            power_[i] += (p - power_[i]) * (p > power_[i] ? att_ : rel_);
        }
    }
    void levelsDb(float* out) const noexcept {
        for (size_t i = 0; i < kCutBands; ++i) out[i] = powerToDb(power_[i]);
    }
private:
    std::array<Svf, kCutBands> bank_{};
    std::array<double, kCutBands> power_{};
    double fs_ = 48000.0, att_ = 0.1, rel_ = 0.01;
};

} // namespace wers
