#pragma once
// A synthetic singing voice with sibilants, for tests and demo renders. Vowels are additive harmonics shaped by
// formants, with vibrato and a breath of noise; "s" and "sz" are filtered noise bursts, louder in the highs
// than any vowel, like a real close-miked voice. Every segment is listed, so tests can measure each one.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace powiew::test {

struct Segment {
    double start, end; // seconds
    bool sibilant;
};

struct Voice {
    std::vector<float> l, r;
    std::vector<Segment> segments;
};

namespace detail {

inline constexpr double kTwoPi = 6.283185307179586;

// A simple biquad band-pass in direct form, enough for shaping test noise.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad highpass(double hz, double q, double fs) {
        const double w = kTwoPi * hz / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        Biquad f;
        f.b0 = (1 + c) / 2 / a0;
        f.b1 = -(1 + c) / a0;
        f.b2 = (1 + c) / 2 / a0;
        f.a1 = -2 * c / a0;
        f.a2 = (1 - al) / a0;
        return f;
    }
    static Biquad lowpass(double hz, double q, double fs) {
        const double w = kTwoPi * hz / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        Biquad f;
        f.b0 = (1 - c) / 2 / a0;
        f.b1 = (1 - c) / a0;
        f.b2 = (1 - c) / 2 / a0;
        f.a1 = -2 * c / a0;
        f.a2 = (1 - al) / a0;
        return f;
    }
    double operator()(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Noise {
    uint32_t s;
    explicit Noise(uint32_t seed) : s(seed * 2654435761u + 7u) {}
    double operator()() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<double>(s) / 4294967296.0 * 2.0 - 1.0;
    }
};

struct Vowel {
    double f[4], g[4], bw[4];
};

// a, i, o, e: the first four formants of a woman's or a tenor's voice.
inline const Vowel kVowels[4] = {
    { { 800, 1150, 2900, 3500 }, { 1.0, 0.63, 0.25, 0.18 }, { 90, 110, 160, 250 } },
    { { 300, 2300, 3000, 3700 }, { 1.0, 0.35, 0.30, 0.20 }, { 70, 120, 170, 250 } },
    { { 500, 900, 2600, 3500 }, { 1.0, 0.70, 0.18, 0.12 }, { 80, 100, 160, 250 } },
    { { 450, 1900, 2600, 3600 }, { 1.0, 0.45, 0.28, 0.18 }, { 80, 120, 160, 250 } },
};

inline double raised(double t, double rise) { return t >= rise ? 1.0 : 0.5 - 0.5 * std::cos(kTwoPi * 0.5 * t / rise); }

} // namespace detail

// One phrase is 1,6 s: "sa", "si", "o-sz", "e", repeated. level scales everything.
inline Voice voiceLoop(double fs, double seconds, double level = 1.0) {
    using namespace detail;
    const size_t n = static_cast<size_t>(fs * seconds);
    Voice v;
    v.l.assign(n, 0.0f);
    std::vector<double> out(n, 0.0);

    struct Note {
        double start, end;
        int vowel;
        double f0;
    };
    struct Hiss {
        double start, end;
        double lo, hi, amp;
    };
    const Note notes[4] = { { 0.13, 0.46, 0, 196.0 }, { 0.58, 0.86, 1, 220.0 }, { 0.92, 1.16, 2, 247.0 }, { 1.30, 1.56, 3, 175.0 } };
    const Hiss hisses[3] = { { 0.00, 0.13, 4800.0, 11000.0, 0.16 }, { 0.47, 0.58, 4800.0, 11000.0, 0.15 }, { 1.16, 1.29, 2600.0, 7500.0, 0.12 } };
    constexpr double kPhrase = 1.6;

    for (double base = 0.0; base < seconds; base += kPhrase) {
        // Vowels: harmonics up to 10 kHz, each with its own phase, shaped by the formants and a gentle tilt.
        for (const auto& note : notes) {
            const double t0 = base + note.start, t1 = std::min(seconds, base + note.end);
            if (t0 >= seconds) continue;
            const auto& vw = kVowels[note.vowel];
            const size_t i0 = static_cast<size_t>(t0 * fs), i1 = static_cast<size_t>(t1 * fs);
            const int harmonics = static_cast<int>(10000.0 / note.f0);
            std::vector<double> amp(static_cast<size_t>(harmonics) + 1), phase(static_cast<size_t>(harmonics) + 1, 0.0);
            double total = 0.0;
            for (int h = 1; h <= harmonics; ++h) {
                const double f = note.f0 * h;
                double shape = 0.05;
                for (int k = 0; k < 4; ++k) {
                    const double d = (f - vw.f[k]) / (0.5 * vw.bw[k]);
                    shape += vw.g[k] / std::sqrt(1.0 + d * d);
                }
                amp[static_cast<size_t>(h)] = shape / (1.0 + f / 400.0);
                phase[static_cast<size_t>(h)] = 0.37 * h * h;
                total += amp[static_cast<size_t>(h)] * amp[static_cast<size_t>(h)];
            }
            const double norm = 0.16 / std::sqrt(0.5 * total); // about 0.16 RMS
            double cycle = 0.0;
            Noise breath(static_cast<uint32_t>(i0 + 3));
            auto bh = Biquad::highpass(2500.0, 0.7, fs), bl = Biquad::lowpass(9000.0, 0.7, fs);
            for (size_t i = i0; i < i1; ++i) {
                const double t = static_cast<double>(i - i0) / fs, len = t1 - t0;
                const double env = raised(t, 0.025) * raised(len - t, 0.05);
                const double vib = 1.0 + 0.015 * std::sin(kTwoPi * 5.5 * t) * std::min(1.0, t / 0.15);
                cycle += note.f0 * vib / fs;
                double s = 0.0;
                for (int h = 1; h <= harmonics; ++h) s += amp[static_cast<size_t>(h)] * std::sin(kTwoPi * h * cycle + phase[static_cast<size_t>(h)]);
                const double air = bl(bh(breath())) * 0.006;
                out[i] += level * env * (norm * s + air);
            }
            v.segments.push_back({ t0, t1, false });
        }
        // Sibilants: noise in their band, rising over 20 ms and falling over 30 ms.
        for (const auto& hs : hisses) {
            const double t0 = base + hs.start, t1 = std::min(seconds, base + hs.end);
            if (t0 >= seconds) continue;
            const size_t i0 = static_cast<size_t>(t0 * fs), i1 = static_cast<size_t>(t1 * fs);
            Noise noise(static_cast<uint32_t>(i0 + 11));
            auto h1 = Biquad::highpass(hs.lo, 0.7, fs), h2 = Biquad::highpass(hs.lo, 0.7, fs), lp = Biquad::lowpass(hs.hi, 0.7, fs);
            for (size_t i = i0; i < i1; ++i) {
                const double t = static_cast<double>(i - i0) / fs, len = t1 - t0;
                const double env = raised(t, 0.02) * raised(len - t, 0.03);
                out[i] += level * env * hs.amp * 2.2 * lp(h2(h1(noise())));
            }
            v.segments.push_back({ t0, t1, true });
        }
    }
    std::sort(v.segments.begin(), v.segments.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
    for (size_t i = 0; i < n; ++i) v.l[i] = static_cast<float>(out[i]);
    v.r = v.l;
    return v;
}

} // namespace powiew::test
