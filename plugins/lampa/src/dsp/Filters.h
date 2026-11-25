#pragma once
// Small filters: biquads with the usual cookbook designs and a one-pole smoother.
#include "Oversampler.h"
#include <cmath>

namespace lampa {

struct BiquadCoefs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoefs highpass(double fs, double hz, double q) {
        const double w = 2.0 * kPi * clampHz(fs, hz) / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return make((1.0 + c) * 0.5 / a0, -(1.0 + c) / a0, (1.0 + c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0);
    }

    static BiquadCoefs peak(double fs, double hz, double q, double db) {
        const double a = std::pow(10.0, db / 40.0);
        const double w = 2.0 * kPi * clampHz(fs, hz) / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha / a;
        return make((1.0 + alpha * a) / a0, -2.0 * c / a0, (1.0 - alpha * a) / a0, -2.0 * c / a0, (1.0 - alpha / a) / a0);
    }

    static BiquadCoefs highShelf(double fs, double hz, double slope, double db) {
        const double a = std::pow(10.0, db / 40.0);
        const double w = 2.0 * kPi * clampHz(fs, hz) / fs, c = std::cos(w), s = std::sin(w);
        const double alpha = s * 0.5 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
        const double k = 2.0 * std::sqrt(a) * alpha;
        const double a0 = (a + 1.0) - (a - 1.0) * c + k;
        return make(a * ((a + 1.0) + (a - 1.0) * c + k) / a0, -2.0 * a * ((a - 1.0) + (a + 1.0) * c) / a0,
                    a * ((a + 1.0) + (a - 1.0) * c - k) / a0, 2.0 * ((a - 1.0) - (a + 1.0) * c) / a0,
                    ((a + 1.0) - (a - 1.0) * c - k) / a0);
    }

private:
    static double clampHz(double fs, double hz) { return std::fmin(std::fmax(hz, 1.0), fs * 0.45); }
    static BiquadCoefs make(double b0, double b1, double b2, double a1, double a2) {
        return { b0, b1, b2, a1, a2 };
    }
};

// Transposed direct form II: coefficients can change between samples without a jump in the state.
// In double: with poles this close to one (10 Hz at 48 kHz), float rounding comes out as rumble.
struct Biquad {
    double s1 = 0.0, s2 = 0.0;
    void reset() { s1 = s2 = 0.0; }
    float process(float in, const BiquadCoefs& c) {
        const double x = in;
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }
};

// Coefficient of a one-pole smoother that covers about 63% of a step in `seconds`.
inline float onePole(double fs, double seconds) {
    if (seconds <= 0.0) return 1.0f;
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * fs)));
}

} // namespace lampa
