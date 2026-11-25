#pragma once
// Sidechain signals for screenshots and demo renders: a kick on every beat of the 120 BPM demo loop, and a
// voice-like line (a sung vowel, two bars on, two bars off) for the ducking presets.
#include "TestAudio.h"
#include <cmath>

namespace doc::test {

inline wir::test::Stereo kickTrack(double fs, double seconds) {
    wir::test::Stereo k{ std::vector<float>(static_cast<size_t>(fs * seconds)), {} };
    for (size_t i = 0; i < k.l.size(); ++i) {
        const double t = std::fmod(static_cast<double>(i) / fs, 0.5);
        k.l[i] = static_cast<float>(0.8 * std::exp(-t * 14.0) * std::sin(2.0 * 3.14159265358979 * (50.0 + 90.0 * std::exp(-t * 30.0)) * t));
    }
    k.r = k.l;
    return k;
}

inline wir::test::Stereo voiceTrack(double fs, double seconds) {
    wir::test::Stereo v{ std::vector<float>(static_cast<size_t>(fs * seconds)), {} };
    double phase = 0.0;
    for (size_t i = 0; i < v.l.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const bool on = std::fmod(t, 8.0) < 4.0;
        const double env = on ? std::min(1.0, std::fmod(t, 8.0) * 8.0) * std::min(1.0, (4.0 - std::fmod(t, 8.0)) * 8.0) : 0.0;
        phase += (180.0 + 8.0 * std::sin(2.0 * 3.14159265358979 * 5.0 * t)) / fs;
        double s = 0.0;
        for (int h = 1; h <= 12; ++h) {
            // Harmonics near 700 and 1100 Hz are stronger, which sounds like an open "a".
            const double f = 180.0 * h;
            const double formant = std::exp(-std::pow((f - 700.0) / 250.0, 2.0)) + 0.6 * std::exp(-std::pow((f - 1100.0) / 300.0, 2.0)) + 0.08;
            s += formant * std::sin(2.0 * 3.14159265358979 * h * phase) / h;
        }
        v.l[i] = static_cast<float>(0.35 * env * s);
    }
    v.r = v.l;
    return v;
}

} // namespace doc::test
