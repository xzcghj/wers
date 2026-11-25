#pragma once
// Material a compressor can show itself on: everything here changes in level on purpose.
#include "TestAudio.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace lampa::test {

using wir::test::Stereo;

inline constexpr double kTwoPi = 6.283185307179586;

// Scales both channels so the loudest sample sits at peakDb.
inline Stereo normalised(Stereo s, double peakDb) {
    float peak = 1e-9f;
    for (size_t i = 0; i < s.l.size(); ++i) peak = std::max({ peak, std::abs(s.l[i]), std::abs(s.r[i]) });
    const float g = static_cast<float>(std::pow(10.0, peakDb / 20.0)) / peak;
    for (size_t i = 0; i < s.l.size(); ++i) {
        s.l[i] *= g;
        s.r[i] *= g;
    }
    return s;
}

// The demo loop with its level moving: a quiet verse, a loud chorus, a swell in between.
inline Stereo dynamicLoop(double fs, double seconds) {
    auto s = wir::test::demoLoop(fs, seconds);
    for (size_t i = 0; i < s.l.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const double phase = std::fmod(t, 8.0) / 8.0;
        const double db = phase < 0.4 ? -12.0 : (phase < 0.55 ? -12.0 + 14.0 * (phase - 0.4) / 0.15 : 2.0);
        const float g = static_cast<float>(std::pow(10.0, db / 20.0));
        s.l[i] *= g;
        s.r[i] *= g;
    }
    return s;
}

// A sung line: a buzzing source through three vowel formants, with vibrato and syllables of uneven loudness.
inline Stereo vocal(double fs, double seconds) {
    const size_t n = static_cast<size_t>(fs * seconds);
    Stereo out{ std::vector<float>(n), std::vector<float>(n) };
    const double notes[] = { 220.0, 246.94, 261.63, 293.66, 261.63, 246.94, 196.0, 220.0 };
    const double loud[] = { 0.5, 0.9, 0.35, 1.0, 0.6, 0.25, 0.8, 0.45 };
    const double formants[2][3] = { { 700.0, 1220.0, 2600.0 }, { 300.0, 2300.0, 3000.0 } }; // "a" and "i"
    double phase = 0.0;
    struct Band { double s1 = 0, s2 = 0; } bands[3];
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const int k = static_cast<int>(t / 0.5) % 8;
        const double inNote = std::fmod(t, 0.5);
        const double f = notes[k] * (1.0 + 0.006 * std::sin(kTwoPi * 5.5 * t));
        phase += f / fs;
        phase -= std::floor(phase);
        const double source = 2.0 * phase - 1.0;
        const double vowel = 0.5 + 0.5 * std::sin(kTwoPi * 0.7 * t);
        double y = 0.0;
        for (int b = 0; b < 3; ++b) {
            const double fc = formants[0][b] + vowel * (formants[1][b] - formants[0][b]);
            const double r = std::exp(-kTwoPi * 90.0 / fs), c = 2.0 * r * std::cos(kTwoPi * fc / fs);
            const double v = (1.0 - r) * source + c * bands[b].s1 - r * r * bands[b].s2;
            bands[b].s2 = bands[b].s1;
            bands[b].s1 = v;
            y += v / (b + 1.0);
        }
        const double env = std::min(1.0, inNote / 0.03) * std::min(1.0, (0.48 - inNote) / 0.06 + 0.05) * loud[k];
        const float v = static_cast<float>(0.9 * std::max(0.0, env) * y);
        out.l[i] = v;
        out.r[i] = v;
    }
    return out;
}

// A bass line played unevenly: some notes hit hard, others barely.
inline Stereo bassLine(double fs, double seconds) {
    const size_t n = static_cast<size_t>(fs * seconds);
    Stereo out{ std::vector<float>(n), std::vector<float>(n) };
    const double notes[] = { 55.0, 55.0, 65.41, 73.42, 49.0, 49.0, 61.74, 73.42 };
    const double vel[] = { 1.0, 0.35, 0.7, 0.5, 0.9, 0.3, 0.6, 0.8 };
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const int k = static_cast<int>(t / 0.25) % 8;
        const double inNote = std::fmod(t, 0.25);
        phase += notes[k] / fs;
        const double x = std::sin(kTwoPi * phase) + 0.35 * std::sin(2.0 * kTwoPi * phase) + 0.15 * std::sin(3.0 * kTwoPi * phase);
        const double env = std::min(1.0, inNote / 0.004) * std::exp(-inNote * 2.5) * vel[k];
        const float v = static_cast<float>(0.45 * env * x);
        out.l[i] = v;
        out.r[i] = v;
    }
    return out;
}

// Drums alone, with an uneven hand: kick, snare and hats.
inline Stereo drums(double fs, double seconds) {
    const size_t n = static_cast<size_t>(fs * seconds);
    Stereo out{ std::vector<float>(n), std::vector<float>(n) };
    uint32_t seed = 777;
    auto rnd = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<double>(seed) / 4294967296.0 * 2.0 - 1.0;
    };
    double snareTone = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double beat = 0.5, inBeat = std::fmod(t, beat);
        const int count = static_cast<int>(t / beat);
        const double accent = (count % 4 == 0) ? 1.0 : 0.7;
        const double kick = (count % 2 == 0) ? accent * std::exp(-inBeat * 16.0) * std::sin(kTwoPi * (45.0 + 90.0 * std::exp(-inBeat * 35.0)) * inBeat) : 0.0;
        const double noise = rnd();
        snareTone += (noise - snareTone) * 0.3;
        const double snare = (count % 2 == 1) ? accent * std::exp(-inBeat * 14.0) * (0.6 * snareTone + 0.4 * std::sin(kTwoPi * 190.0 * inBeat)) : 0.0;
        const double off = std::fmod(t, 0.125);
        const double hat = 0.12 * std::exp(-off * 70.0) * noise * (static_cast<int>(t / 0.125) % 2 ? 0.6 : 1.0);
        out.l[i] = static_cast<float>(0.8 * kick + 0.55 * snare + hat * 0.8);
        out.r[i] = static_cast<float>(0.8 * kick + 0.5 * snare + hat);
    }
    return out;
}

// What fits a preset group.
inline Stereo materialFor(const char* group, double fs, double seconds) {
    const std::string g = group != nullptr ? group : "";
    // Peaks a few dB under full scale, the way tracks usually arrive.
    if (g == "Wokal") return normalised(vocal(fs, seconds), -4.0);
    if (g == "Bas") return normalised(bassLine(fs, seconds), -4.0);
    if (g == "Bęben") return normalised(drums(fs, seconds), -3.0);
    return normalised(dynamicLoop(fs, seconds), -3.0);
}

} // namespace lampa::test
