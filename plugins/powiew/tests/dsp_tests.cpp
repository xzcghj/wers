// Tests of the Powiew engine on its own, without JUCE.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "AllocationCount.h"
#include "AirEngine.h"
#include "TestVoice.h"
#include <cmath>
#include <complex>
#include <vector>

WERS_COUNT_ALLOCATIONS()

using namespace powiew;

namespace {

constexpr double kFs = 48000.0;
using Vec = std::vector<float>;

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed * 2654435761u + 1u) {}
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) * (2.0f / 4294967296.0f) - 1.0f;
    }
};

Vec sine(double hz, double seconds, float amp = 0.5f, double fs = kFs) {
    Vec v(static_cast<size_t>(fs * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs));
    return v;
}

Vec noise(size_t n, float amp, uint32_t seed) {
    Rng r(seed);
    Vec v(n);
    for (auto& x : v) x = amp * r.next();
    return v;
}

Vec add(Vec a, const Vec& b, float k = 1.0f) {
    for (size_t i = 0; i < a.size(); ++i) a[i] += k * b[i];
    return a;
}

void run(AirEngine& e, Vec& l, Vec& r, const Params& p, int block = 256) {
    size_t at = 0;
    int k = 0;
    while (at < l.size()) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block - (k++ % 3) * 37), l.size() - at));
        e.setParams(p);
        e.process(l.data() + at, r.data() + at, n);
        at += static_cast<size_t>(n);
    }
}

struct Out {
    Vec l, r;
};

Out through(const Params& p, const Vec& l, const Vec& r, double fs = kFs, int* latency = nullptr) {
    AirEngine e;
    e.prepare(fs);
    Out o{ l, r };
    run(e, o.l, o.r, p);
    if (latency) *latency = e.latency();
    return o;
}

// The output moved back by the latency, so it lines up with the input.
Vec aligned(const Vec& out, int latency) {
    Vec v(out.size(), 0.0f);
    for (size_t i = 0; i + static_cast<size_t>(latency) < out.size(); ++i) v[i] = out[i + static_cast<size_t>(latency)];
    return v;
}

// Amplitude of one frequency (Goertzel, Blackman-Harris window) over [from, to).
double levelAt(const Vec& x, double hz, size_t from, size_t to, double fs = kFs) {
    const double w = 2.0 * kPi * hz / fs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0, wsum = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i) {
        const double t = 2.0 * kPi * static_cast<double>(i - from) / n;
        const double win = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        wsum += win;
        const double s0 = x[i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / wsum;
}

double db(double ratio) { return 20.0 * std::log10(std::max(1e-15, ratio)); }

// Power spectrum by Welch's method: Hann windows of 2048, half overlap.
std::vector<double> spectrum(const Vec& x, size_t from, size_t to) {
    constexpr size_t kN = 2048;
    std::vector<double> power(kN / 2 + 1, 0.0);
    std::vector<std::complex<double>> buf(kN);
    int frames = 0;
    for (size_t at = from; at + kN <= to; at += kN / 2) {
        for (size_t i = 0; i < kN; ++i) buf[i] = x[at + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / kN));
        // Iterative radix-2 FFT.
        for (size_t i = 1, j = 0; i < kN; ++i) {
            size_t bit = kN >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(buf[i], buf[j]);
        }
        for (size_t len = 2; len <= kN; len <<= 1) {
            const auto wl = std::polar(1.0, -2.0 * kPi / static_cast<double>(len));
            for (size_t i = 0; i < kN; i += len) {
                std::complex<double> w(1.0);
                for (size_t j = 0; j < len / 2; ++j) {
                    const auto u = buf[i + j], v = buf[i + j + len / 2] * w;
                    buf[i + j] = u + v;
                    buf[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        for (size_t b = 0; b < power.size(); ++b) power[b] += std::norm(buf[b]);
        ++frames;
    }
    for (auto& p : power) p /= std::max(1, frames);
    return power;
}

double bandEnergy(const std::vector<double>& power, double lo, double hi, double fs = kFs) {
    const double bin = fs / 2048.0;
    double sum = 0.0;
    for (size_t b = 0; b < power.size(); ++b) {
        const double f = static_cast<double>(b) * bin;
        if (f >= lo && f < hi) sum += power[b];
    }
    return sum;
}

double bandGainDb(const Vec& in, const Vec& outAligned, double lo, double hi, size_t from, size_t to, double fs = kFs) {
    return 10.0 * std::log10(bandEnergy(spectrum(outAligned, from, to), lo, hi, fs) / bandEnergy(spectrum(in, from, to), lo, hi, fs));
}

float peak(const Vec& x, size_t from = 0) {
    float m = 0.0f;
    for (size_t i = from; i < x.size(); ++i) m = std::max(m, std::abs(x[i]));
    return m;
}

Params both(float presence, float air, Character c = Character::Silk, float smooth = 0.0f) {
    Params p;
    p.presence = presence;
    p.air = air;
    p.character = c;
    p.smooth = smooth;
    return p;
}

// HF energy (4,5-16 kHz) gain of the output over the input, inside a segment (skipping its edges).
double segmentGainDb(const Vec& in, const Vec& outAligned, const powiew::test::Segment& s, double lo = 4500.0, double hi = 16000.0) {
    const size_t from = static_cast<size_t>((s.start + 0.02) * kFs), to = static_cast<size_t>((s.end - 0.02) * kFs);
    double ei = 0.0, eo = 0.0;
    // Band energy by FFT over the segment: zero-padded frames are fine, the ratio is what matters.
    const auto pi = spectrum(in, from, std::max(to, from + 2048)), po = spectrum(outAligned, from, std::max(to, from + 2048));
    ei = bandEnergy(pi, lo, hi);
    eo = bandEnergy(po, lo, hi);
    return 10.0 * std::log10(eo / ei);
}

// An 8th-order Butterworth low-pass at 300 Hz in double: what is left of a high-frequency test signal after it
// is only what a click would leave, since clicks spread down to the lowest frequencies and gliding gains do not.
Vec lowFrom(const Vec& x, double fs = kFs) {
    const double qs[4] = { 0.5098, 0.6013, 0.9000, 2.5629 };
    Vec y = x;
    for (double q : qs) {
        powiew::test::detail::Biquad f = powiew::test::detail::Biquad::lowpass(300.0, q, fs);
        for (auto& v : y) v = static_cast<float>(f(v));
    }
    return y;
}

} // namespace

TEST_CASE("the oversampler round trip is a plain delay of a whole number of samples") {
    for (int factor : { 2, 4 }) {
        for (double fs : { 44100.0, 48000.0 }) {
            Oversampler os;
            os.prepare(factor);
            const int lat = os.latency();
            CAPTURE(factor);
            CAPTURE(fs);
            for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 }) {
                CAPTURE(hz);
                const auto x = sine(hz, 0.2, 0.5f, fs);
                Vec y(x.size());
                float hi[4];
                for (size_t i = 0; i < x.size(); ++i) {
                    os.up(x[i], hi);
                    y[i] = os.down(hi);
                }
                float err = 0.0f;
                for (size_t i = 1000; i < x.size(); ++i) err = std::max(err, std::abs(y[i] - x[i - static_cast<size_t>(lat)]));
                CHECK(err < 2e-4f);
            }
        }
    }
}

TEST_CASE("with both knobs at zero the output is the input, delayed by the latency, bit for bit") {
    const auto l = noise(48000, 0.7f, 1), r = add(noise(48000, 0.4f, 2), l, 0.3f);
    for (int c = 0; c < 3; ++c) {
        for (bool mono : { false, true }) {
            CAPTURE(c);
            CAPTURE(mono);
            Params p;
            p.character = static_cast<Character>(c);
            p.monoSafe = mono;
            p.smooth = 0.8f;
            p.mix = 0.6f;
            p.presenceHz = 4000.0f;
            p.airHz = 16000.0f;
            int lat = 0;
            const auto o = through(p, l, r, kFs, &lat);
            CHECK(lat > 0);
            bool same = true;
            for (size_t i = static_cast<size_t>(lat); i < l.size(); ++i)
                if (o.l[i] != l[i - static_cast<size_t>(lat)] || o.r[i] != r[i - static_cast<size_t>(lat)]) same = false;
            CHECK(same);
        }
    }
}

TEST_CASE("the latency is 1,5 ms or the oversampler's, whichever is longer") {
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) {
        AirEngine e;
        e.prepare(fs);
        CAPTURE(fs);
        CHECK(e.latency() >= static_cast<int>(std::lround(0.0015 * fs)));
        Vec l(4096, 0.0f), r(4096, 0.0f);
        l[100] = 1.0f;
        run(e, l, r, Params{});
        CHECK(l[100 + static_cast<size_t>(e.latency())] == 1.0f);
    }
}

TEST_CASE("the EQ part matches the drawn curve and never dips below the dry level") {
    for (int c = 0; c < 3; ++c) {
        CAPTURE(c);
        const auto& s = kShapes[static_cast<size_t>(c)];
        Params p = both(1.0f, 1.0f, static_cast<Character>(c));
        for (double hz : { 150.0, 800.0, 2500.0, 4000.0, 6000.0, 9000.0, 12000.0, 16000.0, 19000.0 }) {
            CAPTURE(hz);
            const auto x = sine(hz, 0.6, 0.1f);
            int lat = 0;
            const auto o = through(p, x, x, kFs, &lat);
            const auto y = aligned(o.l, lat);
            const double measured = db(levelAt(y, hz, 12000, 26000) / levelAt(x, hz, 12000, 26000));
            const double drawn = boostDbAt(hz, kFs, 6000.0, amountForDb(s.presenceDb), 12000.0, amountForDb(s.airDb));
            CHECK(std::abs(measured - drawn) < 0.35);
            CHECK(measured > -0.05);
            if (hz < 300.0) CHECK(std::abs(measured) < 0.05);
        }
    }
    // The centre of the bell gets what the character promises.
    const auto x = sine(6000.0, 0.6, 0.1f);
    int lat = 0;
    const auto o = through(both(1.0f, 0.0f, Character::Shine), x, x, kFs, &lat);
    CHECK(db(levelAt(aligned(o.l, lat), 6000.0, 12000, 26000) / levelAt(x, 6000.0, 12000, 26000)) == doctest::Approx(kShapes[1].presenceDb).epsilon(0.05));
}

TEST_CASE("each knob adds energy to its own band and leaves the low end alone") {
    // A dull signal, falling 6 dB an octave: there is room for air, so the dynamics do not hold it back.
    Vec dull(static_cast<size_t>(kFs * 3.0));
    {
        Rng rng(5);
        double lp = 0.0;
        for (auto& v : dull) {
            lp += 0.05 * (rng.next() - lp);
            v = static_cast<float>(lp * 1.5);
        }
    }
    const size_t from = 12000, to = dull.size() - 4000;
    for (float smooth : { 0.0f, 0.5f }) {
        CAPTURE(smooth);
        int lat = 0;
        auto o = through(both(1.0f, 0.0f, Character::Silk, smooth), dull, dull, kFs, &lat);
        auto y = aligned(o.l, lat);
        const double low = bandGainDb(dull, y, 50, 1000, from, to), pres = bandGainDb(dull, y, 5000, 8000, from, to), top = bandGainDb(dull, y, 16000, 20000, from, to);
        MESSAGE("presence: low " << low << " presence " << pres << " top " << top);
        CHECK(std::abs(low) < 0.1);
        CHECK(pres > (smooth > 0.0f ? 3.0 : 4.0));
        CHECK(top < pres);
        o = through(both(0.0f, 1.0f, Character::Silk, smooth), dull, dull, kFs, &lat);
        y = aligned(o.l, lat);
        const double low2 = bandGainDb(dull, y, 50, 1000, from, to), mid2 = bandGainDb(dull, y, 2000, 4000, from, to), air2 = bandGainDb(dull, y, 14000, 19000, from, to);
        MESSAGE("air: low " << low2 << " 2-4k " << mid2 << " 14-19k " << air2);
        CHECK(std::abs(low2) < 0.1);
        CHECK(mid2 < 1.0);
        CHECK(air2 > (smooth > 0.0f ? 3.5 : 4.5));
    }
}

TEST_CASE("on material that is already bright the boost backs off") {
    // White noise is all highs: next to its body the top is far above any threshold.
    const auto white = noise(static_cast<size_t>(kFs * 2.0), 0.3f, 9);
    int lat = 0;
    const auto still = aligned(through(both(0.0f, 1.0f, Character::Silk, 0.0f), white, white, kFs, &lat).l, lat);
    const auto held = aligned(through(both(0.0f, 1.0f, Character::Silk, 0.6f), white, white, kFs, &lat).l, lat);
    const double gStill = bandGainDb(white, still, 12000, 20000, 12000, white.size() - 4000);
    const double gHeld = bandGainDb(white, held, 12000, 20000, 12000, white.size() - 4000);
    MESSAGE("white noise air gain: static " << gStill << " dB, smooth 60% " << gHeld << " dB");
    CHECK(gStill > 4.5);
    CHECK(gHeld < gStill - 4.0);
}

TEST_CASE("the exciter makes new harmonics: even ones in silk, odd ones in shine, more in analog") {
    const auto x = sine(2500.0, 1.0, 0.3f);
    double h2[3], h3[3], total[3];
    for (int c = 0; c < 3; ++c) {
        int lat = 0;
        const auto y = aligned(through(both(1.0f, 0.0f, static_cast<Character>(c)), x, x, kFs, &lat).l, lat);
        const double f = levelAt(y, 2500.0, 12000, 44000);
        h2[c] = db(levelAt(y, 5000.0, 12000, 44000) / f);
        h3[c] = db(levelAt(y, 7500.0, 12000, 44000) / f);
        total[c] = 0.0;
        for (int h = 2; h <= 6; ++h) total[c] += std::pow(levelAt(y, 2500.0 * h, 12000, 44000) / f, 2.0);
        MESSAGE("character " << c << ": H2 " << h2[c] << " dB, H3 " << h3[c] << " dB, all " << 10 * std::log10(total[c]) << " dB");
        CHECK(h2[c] > -40.0);
        CHECK(h3[c] > -45.0);
    }
    CHECK(db(levelAt(x, 5000.0, 12000, 44000) / 0.3) < -100.0); // the input had none
    CHECK(h2[0] - h3[0] > h2[1] - h3[1] + 6.0);
    CHECK(h3[1] > h2[1]);
    CHECK(total[2] > total[0] * 1.5);
    // The air band makes its harmonics from higher up.
    const auto x6 = sine(6000.0, 1.0, 0.3f);
    int lat = 0;
    const auto y6 = aligned(through(both(0.0f, 1.0f, Character::Analog), x6, x6, kFs, &lat).l, lat);
    CHECK(db(levelAt(y6, 12000.0, 12000, 44000) / levelAt(y6, 6000.0, 12000, 44000)) > -35.0);
    CHECK(db(levelAt(y6, 18000.0, 12000, 44000) / levelAt(y6, 6000.0, 12000, 44000)) > -40.0);
}

TEST_CASE("at 44,1 kHz the harmonics above the audio band do not fold back as aliasing") {
    constexpr double fs = 44100.0, hz = 7500.0;
    const auto x = sine(hz, 1.0, 0.5f, fs);
    // Harmonics 4 to 8 of 7,5 kHz would fold to these if they were not filtered away.
    const double folds[] = { 14100.0, 6600.0, 900.0, 8400.0, 15900.0 };
    int lat = 0;
    Params p = both(1.0f, 1.0f, Character::Analog);
    const auto y = aligned(through(p, x, x, fs, &lat).l, lat);
    const double f = levelAt(y, hz, 12000, 40000, fs);
    CHECK(db(levelAt(y, 15000.0, 12000, 40000, fs) / f) > -40.0); // the 2nd harmonic is there
    double worst = -300.0;
    for (double a : folds) {
        MESSAGE("  at " << a << " Hz: " << db(levelAt(y, a, 12000, 40000, fs) / f) << " dB");
        worst = std::max(worst, db(levelAt(y, a, 12000, 40000, fs) / f));
    }
    MESSAGE("worst alias with 4x oversampling: " << worst << " dB");
    CHECK(worst < -90.0);

    // The same exciter without oversampling: the measurement does see aliasing when it is there.
    Exciter naive;
    naive.prepare(fs, 1, 0);
    naive.setBand(12000.0 / 2.8, 12000.0 / 1.3, 0.72 * 12000.0, 0.0);
    const float w[4] = { kShapes[2].h2, kShapes[2].h3, kShapes[2].h4, kShapes[2].h5 };
    Vec h(x.size());
    for (size_t i = 0; i < x.size(); ++i) h[i] = x[i] + naive.process(x[i], w);
    double naiveWorst = -300.0;
    for (double a : folds) naiveWorst = std::max(naiveWorst, db(levelAt(h, a, 12000, 40000, fs) / levelAt(h, hz, 12000, 40000, fs)));
    MESSAGE("worst alias without oversampling: " << naiveWorst << " dB");
    CHECK(naiveWorst > -60.0);
}

TEST_CASE("loud sibilants get far less boost than the vowels around them") {
    const auto v = powiew::test::voiceLoop(kFs, 6.4);
    const Params smoothOff = both(0.8f, 0.9f, Character::Silk, 0.0f), smoothOn = both(0.8f, 0.9f, Character::Silk, 0.6f);
    int lat = 0;
    const auto off = aligned(through(smoothOff, v.l, v.r, kFs, &lat).l, lat);
    const auto on = aligned(through(smoothOn, v.l, v.r, kFs, &lat).l, lat);
    double vowelOff = 0, vowelOn = 0, sibOff = 0, sibOn = 0;
    int vowels = 0, sibs = 0;
    for (const auto& s : v.segments) {
        if (s.start < 0.2 || s.end > 6.3) continue;
        if (s.sibilant) {
            sibOff += segmentGainDb(v.l, off, s);
            sibOn += segmentGainDb(v.l, on, s);
            ++sibs;
        } else {
            vowelOff += segmentGainDb(v.l, off, s);
            vowelOn += segmentGainDb(v.l, on, s);
            ++vowels;
        }
    }
    vowelOff /= vowels;
    vowelOn /= vowels;
    sibOff /= sibs;
    sibOn /= sibs;
    MESSAGE("vowels: static " << vowelOff << " dB, dynamic " << vowelOn << " dB; sibilants: static " << sibOff << " dB, dynamic " << sibOn << " dB");
    CHECK(vowelOn > 2.5);
    CHECK(vowelOn > vowelOff - 1.0);
    CHECK(sibOff > 2.0);
    CHECK(sibOn < 1.0);
    CHECK(sibOn < sibOff - 2.0);
}

TEST_CASE("the dynamics do not depend on the input level") {
    const auto loud = powiew::test::voiceLoop(kFs, 3.2), quiet = powiew::test::voiceLoop(kFs, 3.2, 0.1);
    const Params p = both(0.6f, 0.7f, Character::Silk, 0.6f);
    int lat = 0;
    const auto a = aligned(through(p, loud.l, loud.r, kFs, &lat).l, lat);
    const auto b = aligned(through(p, quiet.l, quiet.r, kFs, &lat).l, lat);
    for (const auto& s : loud.segments) {
        if (s.start < 0.2 || s.end > 3.1) continue;
        CAPTURE(s.start);
        CHECK(std::abs(segmentGainDb(loud.l, a, s) - segmentGainDb(quiet.l, b, s)) < 0.5);
    }
}

TEST_CASE("attacks get a little more air than what follows them") {
    // A 300 Hz body starting every 400 ms, and a quiet 6 kHz tone all the time.
    Vec x(static_cast<size_t>(kFs * 3.2));
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / kFs, inHit = std::fmod(t, 0.4);
        x[i] = static_cast<float>(0.4 * std::exp(-inHit * 6.0) * std::sin(2 * kPi * 300 * t) + 0.01 * std::sin(2 * kPi * 6000 * t));
    }
    int lat = 0;
    const auto y = aligned(through(both(1.0f, 0.0f, Character::Shine, 0.0f), x, x, kFs, &lat).l, lat);
    double early = 0.0, late = 0.0;
    for (int hit = 2; hit < 8; ++hit) {
        const size_t at = static_cast<size_t>(hit * 0.4 * kFs);
        early += db(levelAt(y, 6000.0, at, at + 960) / levelAt(x, 6000.0, at, at + 960));
        late += db(levelAt(y, 6000.0, at + 9600, at + 14400) / levelAt(x, 6000.0, at + 9600, at + 14400));
    }
    MESSAGE("6 kHz boost on attacks " << early / 6 << " dB, later " << late / 6 << " dB");
    CHECK(early / 6 > late / 6 + 1.0);
}

TEST_CASE("mono-safe adds nothing to the sides") {
    const auto l = noise(48000, 0.5f, 3), r0 = noise(48000, 0.5f, 4);
    Params p = both(1.0f, 1.0f, Character::Analog, 0.3f);
    p.monoSafe = true;
    // Pure side in: exactly the same out.
    Vec negL(l.size());
    for (size_t i = 0; i < l.size(); ++i) negL[i] = -l[i];
    int lat = 0;
    const auto o = through(p, l, negL, kFs, &lat);
    bool same = true;
    for (size_t i = static_cast<size_t>(lat) + 2000; i < l.size(); ++i)
        if (o.l[i] != l[i - static_cast<size_t>(lat)] || o.r[i] != negL[i - static_cast<size_t>(lat)]) same = false;
    CHECK(same);
    // Stereo in: the sides are untouched, the mid gets the air.
    const auto r = add(r0, l, 0.5f);
    const auto s = through(p, l, r, kFs, &lat);
    float sideErr = 0.0f;
    for (size_t i = static_cast<size_t>(lat) + 2000; i < l.size(); ++i)
        sideErr = std::max(sideErr, std::abs((s.l[i] - s.r[i]) - (l[i - static_cast<size_t>(lat)] - r[i - static_cast<size_t>(lat)])));
    CHECK(sideErr < 1e-5f);
    // Without it, the sides get air too.
    p.monoSafe = false;
    const auto w = through(p, l, r, kFs, &lat);
    float sideMoved = 0.0f;
    for (size_t i = static_cast<size_t>(lat) + 2000; i < l.size(); ++i)
        sideMoved = std::max(sideMoved, std::abs((w.l[i] - w.r[i]) - (l[i - static_cast<size_t>(lat)] - r[i - static_cast<size_t>(lat)])));
    CHECK(sideMoved > 0.01f);
}

TEST_CASE("listening to the addition, mix and output gain do exactly what they say") {
    const auto v = powiew::test::voiceLoop(kFs, 1.6);
    Params p = both(0.8f, 0.8f, Character::Shine, 0.5f);
    int lat = 0;
    const auto full = through(p, v.l, v.r, kFs, &lat);
    p.delta = true;
    const auto delta = through(p, v.l, v.r, kFs, &lat);
    p.delta = false;
    p.mix = 0.5f;
    const auto half = through(p, v.l, v.r, kFs, &lat);
    p.mix = 1.0f;
    p.outputGain = 2.0f;
    const auto loud = through(p, v.l, v.r, kFs, &lat);
    float eDelta = 0, eMix = 0, eGain = 0;
    for (size_t i = static_cast<size_t>(lat); i < v.l.size(); ++i) {
        const float dry = v.l[i - static_cast<size_t>(lat)];
        eDelta = std::max(eDelta, std::abs(full.l[i] - delta.l[i] - dry));
        eMix = std::max(eMix, std::abs((half.l[i] - dry) - 0.5f * (full.l[i] - dry)));
        eGain = std::max(eGain, std::abs(loud.l[i] - 2.0f * full.l[i]));
    }
    CHECK(eDelta < 1e-5f);
    CHECK(eMix < 1e-5f);
    CHECK(eGain < 1e-5f);
    // What is added lives in the highs: below 500 Hz only the bell's gentle skirt, far under the dry signal.
    const auto lowDelta = spectrum(delta.l, 4000, delta.l.size() - 2000), lowDry = spectrum(v.l, 4000, v.l.size() - 2000);
    CHECK(10 * std::log10(bandEnergy(lowDelta, 50, 500) / bandEnergy(lowDry, 50, 500)) < -25.0);
}

TEST_CASE("bypass fades to the delayed dry signal") {
    AirEngine e;
    e.prepare(kFs);
    auto l = noise(24000, 0.5f, 6), r = noise(24000, 0.5f, 7);
    const auto inL = l;
    Params p = both(1.0f, 1.0f, Character::Shine);
    e.setParams(p);
    e.process(l.data(), r.data(), 4800);
    p.bypass = true;
    e.setParams(p);
    e.process(l.data() + 4800, r.data() + 4800, 19200);
    const auto lat = static_cast<size_t>(e.latency());
    float err = 0.0f;
    for (size_t i = 4800 + 1000 + lat; i < l.size(); ++i) err = std::max(err, std::abs(l[i] - inL[i - lat]));
    CHECK(err == 0.0f);
}

TEST_CASE("moving every control while it plays does not click") {
    // High tones only: a click spreads down to the lowest frequencies, gliding gains do not, so whatever is left
    // below 300 Hz is a click.
    const double seconds = 4.0;
    auto x = add(sine(2400.0, seconds, 0.2f), sine(7000.0, seconds, 0.1f));
    auto l = x, r = add(sine(2400.0, seconds, 0.15f), sine(7300.0, seconds, 0.1f));
    AirEngine e;
    e.prepare(kFs);
    Params p;
    size_t at = 0;
    int block = 0;
    while (at < l.size()) {
        p.presence = (block / 11) % 2 == 0 ? 0.0f : 1.0f;
        p.air = (block / 7) % 2 == 0 ? 1.0f : 0.0f;
        p.presenceHz = (block / 13) % 2 == 0 ? 3000.0f : 10000.0f;
        p.airHz = (block / 17) % 2 == 0 ? 8000.0f : 20000.0f;
        p.smooth = (block / 9) % 2 == 0 ? 0.0f : 1.0f;
        p.character = static_cast<Character>((block / 19) % 3);
        p.mix = (block / 23) % 2 == 0 ? 1.0f : 0.0f;
        p.outputGain = (block / 29) % 2 == 0 ? 0.25f : 4.0f;
        p.monoSafe = (block / 31) % 2 == 1;
        p.delta = (block / 37) % 2 == 1;
        p.bypass = (block / 41) % 2 == 1;
        const int n = static_cast<int>(std::min<size_t>(256, l.size() - at));
        e.setParams(p);
        e.process(l.data() + at, r.data() + at, n);
        at += static_cast<size_t>(n);
        ++block;
    }
    const auto low = lowFrom(l), lowR = lowFrom(r);
    const float worst = std::max(peak(low, 4800), peak(lowR, 4800));
    MESSAGE("left below 300 Hz while every control jumps: " << worst);
    CHECK(worst < 1e-3f);
    // The same measure on a hard gain jump: that would be a click, and it shows.
    auto hard = add(sine(2400.0, 1.0, 0.2f), sine(7000.0, 1.0, 0.1f));
    for (size_t i = 24000; i < hard.size(); ++i) hard[i] *= 0.5f;
    CHECK(peak(lowFrom(hard), 4800) > 3e-3f);
}

TEST_CASE("wild settings stay finite at every sample rate") {
    Rng rng(42);
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) {
        AirEngine e;
        e.prepare(fs);
        for (int round = 0; round < 24; ++round) {
            CAPTURE(fs);
            CAPTURE(round);
            auto u = [&] { return 0.5f + 0.5f * rng.next(); };
            Params p;
            p.presence = u();
            p.air = u();
            p.presenceHz = 2000.0f + 9000.0f * u();
            p.airHz = 7000.0f + 18000.0f * u();
            p.smooth = u();
            p.character = static_cast<Character>(round % 3);
            p.mix = u();
            p.outputGain = 4.0f * u();
            p.monoSafe = u() > 0.5f;
            p.delta = u() > 0.8f;
            const float amp = round % 4 == 0 ? 8.0f : (round % 4 == 1 ? 1e-7f : 1.0f);
            auto l = noise(static_cast<size_t>(fs * 0.1), amp, static_cast<uint32_t>(round + 11)), r = sine(330.0 + round * 41.0, 0.1, amp, fs);
            if (round % 6 == 5) std::fill(l.begin(), l.end(), 0.0f);
            run(e, l, r, p, 64 + round * 29);
            for (size_t i = 0; i < l.size(); ++i) {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::isfinite(r[i]));
            }
            CHECK(peak(l) < 64.0f);
        }
    }
}

TEST_CASE("processing allocates nothing") {
    AirEngine e;
    e.prepare(kFs);
    auto l = noise(4800, 0.5f, 1), r = noise(4800, 0.5f, 2);
    Params p = both(0.7f, 0.9f, Character::Analog, 0.5f);
    wers::test::AllocationCounter counter;
    for (int k = 0; k < 6; ++k) {
        p.character = static_cast<Character>(k % 3);
        p.monoSafe = k % 2 == 1;
        e.setParams(p);
        e.process(l.data(), r.data(), 4800);
    }
    CHECK(counter.stop() == 0);
}

TEST_CASE("the meters show the added air and the backing off") {
    AirEngine e;
    e.prepare(kFs);
    auto v = powiew::test::voiceLoop(kFs, 1.6);
    e.setParams(both(0.8f, 0.8f, Character::Silk, 0.6f));
    float maxBack = 0.0f, maxAdded = 0.0f;
    for (size_t at = 0; at + 256 <= v.l.size(); at += 256) {
        e.process(v.l.data() + at, v.r.data() + at, 256);
        maxBack = std::max(maxBack, e.airBackoff());
        maxAdded = std::max(maxAdded, e.presenceAddedDb());
    }
    CHECK(maxBack > 0.5f);
    CHECK(maxAdded > 1.5f);
    std::vector<float> out(4096), added(4096);
    e.copySpectrumSources(out.data(), added.data(), 4096);
    CHECK(peak(Vec(out.begin(), out.end())) > 0.01f);
    CHECK(peak(Vec(added.begin(), added.end())) > 0.0001f);
}
