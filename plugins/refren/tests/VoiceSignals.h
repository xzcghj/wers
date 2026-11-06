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

struct Formant {
    double hz, bandwidth;
};

inline const std::vector<Formant> kVowelI{ { 280.0, 60.0 }, { 2250.0, 90.0 }, { 2900.0, 120.0 } };
inline const std::vector<Formant> kVowelU{ { 320.0, 60.0 }, { 870.0, 70.0 }, { 2250.0, 110.0 } };
inline const std::vector<Formant> kVowelY{ { 400.0, 60.0 }, { 1600.0, 90.0 }, { 2500.0, 120.0 } };
inline const std::vector<Formant> kVowelE{ { 420.0, 60.0 }, { 2000.0, 90.0 }, { 2600.0, 120.0 } };
inline const std::vector<Formant> kVowelO{ { 500.0, 70.0 }, { 880.0, 70.0 }, { 2500.0, 120.0 } };
inline const std::vector<Formant> kVowelA{ { 700.0, 90.0 }, { 1100.0, 90.0 }, { 2500.0, 120.0 } };

// A voice built the way a throat builds it: glottal pulses through resonances. Close vowels at some pitches put a
// resonance right on the second harmonic, which then repeats every half period. weakPulse makes every second pulse
// that much weaker, as in a creaky voice. pitchHz moves by 1% three times a second.
inline std::vector<float> throatVoice(double pitchHz, const std::vector<Formant>& formants, int n, double fs, double lowCutHz = 0.0, double weakPulse = 1.0) {
    struct Resonance {
        double a, b, c, y1 = 0.0, y2 = 0.0;
        double tick(double x) {
            const double y = a * x + b * y1 + c * y2;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    std::vector<Resonance> resonances;
    for (const auto& f : formants) {
        const double r = std::exp(-wers::kPi * f.bandwidth / fs);
        const double b = 2.0 * r * std::cos(2.0 * wers::kPi * f.hz / fs), c = -r * r;
        resonances.push_back({ 1.0 - b - c, b, c });
    }
    constexpr double kOpen = 0.6, kRise = 0.7 * kOpen;
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    wers::Svf cutA, cutB;
    cutA.set(std::max(lowCutHz, 1.0), 0.7071, fs);
    cutB.set(std::max(lowCutHz, 1.0), 0.7071, fs);
    double place = 0.0, last = 0.0, peak = 0.0;
    bool weak = false;
    for (int i = 0; i < n; ++i) {
        place += pitchHz * (1.0 + 0.01 * std::sin(2.0 * wers::kPi * 3.0 * i / fs)) / fs;
        if (place >= 1.0) {
            place -= 1.0;
            weak = !weak;
        }
        const double opening = place < kRise ? 0.5 - 0.5 * std::cos(wers::kPi * place / kRise) : place < kOpen ? std::cos(0.5 * wers::kPi * (place - kRise) / (kOpen - kRise)) : 0.0;
        const double glottis = opening * (weak ? weakPulse : 1.0);
        double v = glottis - last; // the lips
        last = glottis;
        for (auto& r : resonances) v = r.tick(v);
        if (lowCutHz > 0.0) v = cutB.tick(cutA.tick(v).hp).hp;
        x[static_cast<size_t>(i)] = static_cast<float>(v);
        peak = std::max(peak, std::abs(v));
    }
    for (auto& v : x) v = static_cast<float>(v * 0.25 / peak);
    return x;
}

} // namespace refren::test
