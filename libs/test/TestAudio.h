#pragma once
// Synthetic music for tests and demo renders: a strummed chord, a bass and a simple beat, stereo.
#include <cmath>
#include <cstdint>
#include <vector>

namespace wir::test {

struct Stereo {
    std::vector<float> l, r;
};

inline Stereo demoLoop(double fs, double seconds, double bpm = 120.0) {
    const size_t n = static_cast<size_t>(fs * seconds);
    Stereo out{ std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
    const double beat = 60.0 / bpm;
    // Chords: Am, F, C, G, one bar each, strummed on every half note.
    const double chords[4][4] = { { 220.0, 261.63, 329.63, 440.0 }, { 174.61, 220.0, 261.63, 349.23 },
                                  { 196.0, 261.63, 329.63, 392.0 }, { 196.0, 246.94, 293.66, 392.0 } };
    uint32_t seed = 12345;
    auto rnd = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed) / 4294967296.0f * 2.0f - 1.0f;
    };
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const int bar = static_cast<int>(t / (4.0 * beat)) % 4;
        const double inHalf = std::fmod(t, 2.0 * beat);
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const double start = k * 0.012;
            if (inHalf < start) continue;
            const double tt = inHalf - start;
            const double f = chords[bar][k];
            const double env = std::exp(-tt * 2.2);
            // A few harmonics, falling off like a plucked string.
            double v = 0.0;
            for (int h = 1; h <= 6; ++h) v += std::sin(2.0 * 3.14159265358979 * f * h * tt) / (h * h * 0.6 + 0.4) * std::exp(-tt * h * 0.8);
            sum += static_cast<float>(0.09 * env * v);
        }
        // Bass on the root.
        const double inBeat = std::fmod(t, beat);
        const float bass = static_cast<float>(0.22 * std::exp(-inBeat * 3.0) * std::sin(2.0 * 3.14159265358979 * chords[bar][0] * 0.25 * t));
        // Kick on every beat, hats on off-beats.
        const float kick = static_cast<float>(0.5 * std::exp(-inBeat * 18.0) * std::sin(2.0 * 3.14159265358979 * (50.0 + 80.0 * std::exp(-inBeat * 30.0)) * inBeat));
        const double off = std::fmod(t + beat * 0.5, beat);
        const float hat = 0.06f * static_cast<float>(std::exp(-off * 60.0)) * rnd();
        out.l[i] = 0.9f * (sum * 1.05f + bass + kick + hat * 0.8f);
        out.r[i] = 0.9f * (sum * 0.95f + bass + kick + hat);
    }
    return out;
}

} // namespace wir::test
