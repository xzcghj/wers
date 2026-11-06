#pragma once
// Synthetic voices with a moving pitch, for what must follow one.
#include "TestSignals.h"

namespace refren::test {

// The pitch of glideVoice at a moment of its length, moving evenly in musical terms.
inline double glideHz(double f0From, double f0To, double seconds, double length) {
    return f0From * std::pow(f0To / f0From, std::clamp(seconds / length, 0.0, 1.0));
}

// A voiced sound like wers::test::vowel, gliding from one pitch to another over its whole length.
inline std::vector<float> glideVoice(double f0From, double f0To, double db, int n, double fs) {
    constexpr double kF1 = 700.0, kF2 = 1200.0;
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    const double length = n / fs;
    const int harmonics = static_cast<int>(std::min(6000.0, fs * 0.45) / std::max(f0From, f0To));
    std::vector<double> phase(static_cast<size_t>(harmonics) + 1, 0.0), amp(static_cast<size_t>(harmonics) + 1, 0.0);
    for (int h = 1; h <= harmonics; ++h) phase[static_cast<size_t>(h)] = h * 0.7;
    for (int i = 0; i < n; ++i) {
        const double f0 = glideHz(f0From, f0To, i / fs, length);
        if (i % 64 == 0) {
            for (int h = 1; h <= harmonics; ++h) {
                const double f = h * f0;
                amp[static_cast<size_t>(h)] = 1.0 / (1.0 + std::pow(f / 500.0, 1.6))
                    * (1.0 + 2.5 / (1.0 + std::pow((f - kF1) / 120.0, 2.0)) + 1.5 / (1.0 + std::pow((f - kF2) / 150.0, 2.0)));
            }
        }
        double sum = 0.0;
        for (int h = 1; h <= harmonics; ++h) {
            phase[static_cast<size_t>(h)] += 2.0 * wers::kPi * h * f0 / fs;
            sum += amp[static_cast<size_t>(h)] * std::sin(phase[static_cast<size_t>(h)]);
        }
        x[static_cast<size_t>(i)] = static_cast<float>(sum);
    }
    double energy = 0.0;
    for (float v : x) energy += static_cast<double>(v) * v;
    const double gain = wers::dbToGainD(db) / std::sqrt(energy / n + 1e-20);
    for (auto& v : x) v = static_cast<float>(v * gain);
    return x;
}

} // namespace refren::test
