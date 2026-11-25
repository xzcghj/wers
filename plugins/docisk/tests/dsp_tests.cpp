// Tests of the Docisk compressor on its own, without JUCE: the curve, the times, the routing and the safety.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "AllocationCount.h"
#include "Compressor.h"
#include <cmath>
#include <functional>
#include <vector>

WERS_COUNT_ALLOCATIONS()

using namespace doc;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed * 2654435761u + 1u) {}
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) * (2.0f / 4294967296.0f) - 1.0f;
    }
    float unit() { return 0.5f * (next() + 1.0f); }
};

using Signal = std::vector<float>;

Signal sine(double hz, double seconds, float amp, double fs = kFs) {
    Signal v(static_cast<size_t>(fs * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs));
    return v;
}

// A square wave keeps its peak level all the time, so a peak detector reads a steady level from it.
Signal square(double hz, double seconds, float amp, double fs = kFs) {
    Signal v(static_cast<size_t>(fs * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = std::fmod(static_cast<double>(i) * hz / fs, 1.0) < 0.5 ? amp : -amp;
    return v;
}

float fromDb(float db) { return std::pow(10.0f, db / 20.0f); }
double toDb(double g) { return 20.0 * std::log10(std::max(1e-12, g)); }

double rms(const Signal& x, size_t from, size_t to) {
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(std::max<size_t>(1, to - from)));
}

double peak(const Signal& x, size_t from, size_t to) {
    double p = 0.0;
    for (size_t i = from; i < to; ++i) p = std::max(p, static_cast<double>(std::abs(x[i])));
    return p;
}

// Level of one frequency by Goertzel with a Hann window, over [from, to).
double levelAt(const Signal& x, double hz, size_t from, size_t to, double fs = kFs) {
    const double w = 2.0 * kPi * hz / fs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i - from) / n);
        const double s0 = x[i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (n * 0.25);
}

Params base() {
    Params p;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 0.0f;
    p.attackMs = 1.0f;
    p.releaseMs = 2000.0f; // a long release keeps a peak detector from rippling with each cycle of a sine
    return p;
}

struct Rig {
    Compressor comp;
    explicit Rig(const Params& p, double fs = kFs) {
        comp.prepare(fs);
        comp.setParams(p);
        comp.reset();
    }
    // Stereo in place, the same signal on both sides unless r is given. Odd block sizes on purpose.
    void run(Signal& l, Signal& r, const Signal* keyL = nullptr, const Signal* keyR = nullptr, int block = 256) {
        size_t at = 0;
        int k = 0;
        while (at < l.size()) {
            const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block - (k++ % 3) * 37), l.size() - at));
            comp.process(l.data() + at, r.data() + at, keyL ? keyL->data() + at : nullptr, keyR ? keyR->data() + at : nullptr, n);
            at += static_cast<size_t>(n);
        }
    }
    Signal mono(Signal x) {
        Signal r = x;
        run(x, r);
        return x;
    }
    // One sample at a time, recording the gain of the left channel after each.
    std::vector<float> gainTrace(const Signal& x) {
        std::vector<float> g(x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            float a = x[i], b = x[i];
            comp.process(&a, &b, nullptr, nullptr, 1);
            g[i] = comp.currentGainDb(0);
        }
        return g;
    }
};

// The steady gain a sine of the given peak level gets: output over input, by RMS, after it settles.
double steadyGainDb(const Params& p, float levelDb, double hz = 1000.0) {
    Rig rig(p);
    const auto in = sine(hz, 0.8, fromDb(levelDb));
    const auto out = rig.mono(in);
    const size_t from = in.size() * 3 / 4;
    return toDb(rms(out, from, in.size()) / rms(in, from, in.size()));
}

// Time from the start of a step until the gain has gone 63,2% of the way to where it ends.
double timeConstantMs(const std::vector<float>& g, size_t from, size_t to) {
    const double start = g[from], end = g[to - 1];
    const double mark = start + 0.632 * (end - start);
    for (size_t i = from; i < to; ++i)
        if ((end < start && g[i] <= mark) || (end > start && g[i] >= mark)) return 1000.0 * static_cast<double>(i - from) / kFs;
    return 1e9;
}

} // namespace

TEST_CASE("the static curve: hard knee, gain reduction at each level is exactly threshold and ratio") {
    for (float ratio : { 2.0f, 4.0f, 10.0f }) {
        auto p = base();
        p.ratio = ratio;
        for (float level = -40.0f; level <= 0.0f; level += 4.0f) {
            CAPTURE(ratio);
            CAPTURE(level);
            const double expected = level > -20.0f ? (level + 20.0f) * (1.0f / ratio - 1.0f) : 0.0;
            CHECK(std::abs(steadyGainDb(p, level) - expected) < 0.15);
        }
    }
}

TEST_CASE("the static curve: soft knee follows the parabola, infinity holds the threshold, range caps it") {
    auto p = base();
    p.kneeDb = 12.0f;
    for (float level : { -30.0f, -26.0f, -23.0f, -20.0f, -17.0f, -14.0f, -10.0f, -4.0f }) {
        CAPTURE(level);
        const double expected = curveGainDb(level, -20.0f, slopeForRatio(4.0f), 12.0f, kNoLimitDb);
        CHECK(std::abs(steadyGainDb(p, level) - expected) < 0.15);
    }
    // In the knee the gain is between none and the full ratio.
    CHECK(curveGainDb(-20.0f, -20.0f, -0.75f, 12.0f, 100.0f) == doctest::Approx(-0.75 * 36.0 / 24.0));

    auto lim = base();
    lim.ratio = kRatioInfinite;
    lim.thresholdDb = -24.0f;
    for (float level : { -18.0f, -12.0f, -3.0f })
        CHECK(std::abs(level + steadyGainDb(lim, level) - (-24.0)) < 0.2); // the output sits on the threshold

    lim.rangeDb = 8.0f;
    CHECK(std::abs(steadyGainDb(lim, -6.0f) - (-8.0)) < 0.15);   // 18 dB over, but at most 8 dB of reduction
    CHECK(std::abs(steadyGainDb(lim, -20.0f) - (-4.0)) < 0.15);  // under the range it still works normally
}

TEST_CASE("below 1:1 the curve expands upwards, as far as the range lets it") {
    auto p = base();
    p.ratio = 0.5f;
    p.rangeDb = 6.0f;
    CHECK(std::abs(steadyGainDb(p, -30.0f)) < 0.1);
    CHECK(std::abs(steadyGainDb(p, -17.0f) - 3.0) < 0.15);
    CHECK(std::abs(steadyGainDb(p, -10.0f) - 6.0) < 0.15);
}

TEST_CASE("all three characters settle on the same static curve") {
    for (int c = 0; c < 3; ++c) {
        auto p = base();
        p.character = static_cast<Character>(c);
        p.kneeDb = 6.0f;
        for (float level : { -30.0f, -21.0f, -18.0f, -12.0f, -4.0f }) {
            CAPTURE(c);
            CAPTURE(level);
            const double expected = curveGainDb(level, -20.0f, -0.75f, 6.0f, kNoLimitDb);
            CHECK(std::abs(steadyGainDb(p, level) - expected) < 0.2);
        }
    }
    // The feedback mapping inverts the curve exactly: heard = level + gain gives back the same gain.
    for (float level = -40.0f; level <= 0.0f; level += 0.5f) {
        const float g = curveGainDb(level, -20.0f, -0.6f, 10.0f, 15.0f);
        float d = 0.0f;
        CHECK(feedbackGainDb(level + g, -20.0f, -0.6f, 10.0f, 15.0f, d) == doctest::Approx(g).epsilon(1e-3).scale(1.0));
    }
}

TEST_CASE("peak and RMS read a steady sine alike, and RMS lets short peaks through") {
    auto peakP = base(), rmsP = base();
    rmsP.rms = 1.0f;
    CHECK(std::abs(steadyGainDb(peakP, -8.0f) - steadyGainDb(rmsP, -8.0f)) < 0.3);
    // A click train: each click is loud, the average is low.
    Signal clicks(static_cast<size_t>(kFs), 0.0f);
    for (size_t i = 0; i < clicks.size(); i += 480)
        for (size_t k = 0; k < 24; ++k) clicks[i + k] = 0.7f * static_cast<float>(std::sin(kPi * static_cast<double>(k) / 24.0));
    Rig a(peakP), b(rmsP);
    a.mono(clicks);
    b.mono(clicks);
    CHECK(a.comp.currentGainDb(0) < b.comp.currentGainDb(0) - 6.0f);
}

TEST_CASE("attack and release times are what the knobs say") {
    for (float attack : { 1.0f, 10.0f, 100.0f }) {
        auto p = base();
        p.thresholdDb = -40.0f;
        p.ratio = kRatioInfinite;
        p.attackMs = attack;
        p.releaseMs = 2000.0f;
        Rig rig(p);
        auto x = square(4000.0, 0.1 + attack * 0.008, 0.0f);
        const auto hit = square(4000.0, attack * 0.008, fromDb(-10.0f));
        x.insert(x.end(), hit.begin(), hit.end());
        const auto g = rig.gainTrace(x);
        CAPTURE(attack);
        const size_t start = static_cast<size_t>((0.1 + attack * 0.008) * kFs);
        const double measured = timeConstantMs(g, start, g.size());
        CHECK(measured == doctest::Approx(attack).epsilon(0.12));
        CHECK(g.back() == doctest::Approx(-30.0f).epsilon(0.02));
    }
    for (float release : { 50.0f, 500.0f, 2000.0f }) {
        auto p = base();
        p.thresholdDb = -40.0f;
        p.ratio = kRatioInfinite;
        p.attackMs = 0.5f;
        p.releaseMs = release;
        Rig rig(p);
        auto x = square(4000.0, 0.3, fromDb(-10.0f));
        const size_t start = x.size();
        x.resize(x.size() + static_cast<size_t>(release * 0.008 * kFs), 0.0f);
        const auto g = rig.gainTrace(x);
        CAPTURE(release);
        CHECK(timeConstantMs(g, start, g.size()) == doctest::Approx(release).epsilon(0.1));
    }
}

TEST_CASE("punch lets the first milliseconds of a hit through, glue keeps the attack time honest") {
    auto p = base();
    p.thresholdDb = -40.0f;
    p.ratio = 4.0f;
    p.attackMs = 10.0f;
    p.releaseMs = 2000.0f;
    std::vector<float> traces[3];
    for (int c = 0; c < 3; ++c) {
        p.character = static_cast<Character>(c);
        Rig rig(p);
        auto x = square(4000.0, 0.05, 0.0f);
        const auto hit = square(4000.0, 0.15, fromDb(-10.0f));
        x.insert(x.end(), hit.begin(), hit.end());
        traces[c] = rig.gainTrace(x);
    }
    const size_t start = static_cast<size_t>(0.05 * kFs), early = start + static_cast<size_t>(0.001 * kFs);
    // One millisecond in, punch has taken less than a third of what clean has.
    CHECK(traces[1][early] > 0.33f * traces[0][early]);
    CHECK(std::abs(traces[1][early]) < 0.33f * std::abs(traces[0][early]));
    // Both still reach the full reduction in about the attack time.
    CHECK(timeConstantMs(traces[1], start, traces[1].size()) == doctest::Approx(10.7).epsilon(0.15));
    CHECK(timeConstantMs(traces[2], start, traces[2].size()) == doctest::Approx(10.0).epsilon(0.35));
    for (auto& t : traces) CHECK(t.back() == doctest::Approx(-22.5f).epsilon(0.02));
}

TEST_CASE("automatic release lets go fast after a short hit and slowly after long compression") {
    auto p = base();
    p.thresholdDb = -40.0f;
    p.ratio = kRatioInfinite;
    p.attackMs = 0.5f;
    p.releaseMs = 400.0f;
    p.autoRelease = true;
    auto releaseAfter = [&](double burst) {
        Rig rig(p);
        auto x = square(4000.0, burst, fromDb(-10.0f));
        const size_t start = x.size();
        x.resize(x.size() + static_cast<size_t>(2.0 * kFs), 0.0f);
        const auto g = rig.gainTrace(x);
        return timeConstantMs(g, start, g.size());
    };
    const double shortHit = releaseAfter(0.03), longHold = releaseAfter(3.0);
    MESSAGE("release after 30 ms: " << shortHit << " ms, after 3 s: " << longHold << " ms");
    CHECK(shortHit < 150.0);
    CHECK(longHold > 300.0);
    CHECK(longHold > 2.5 * shortHit);
}

TEST_CASE("lookahead delays everything by exactly its latency and the gain arrives before the hit") {
    auto p = base();
    p.lookaheadMs = 5.0f;
    p.mix = 0.0f;
    Rig rig(p);
    CHECK(rig.comp.latencySamples() == 240);
    Signal x(2000, 0.0f);
    x[100] = 0.5f;
    const auto y = rig.mono(x);
    CHECK(y[340] == doctest::Approx(0.5f));
    CHECK(peak(y, 0, 340) < 1e-6);

    // A hard hit after silence: with lookahead the first millisecond is already held down.
    auto hitPeak = [&](float lookahead) {
        auto q = base();
        q.thresholdDb = -30.0f;
        q.ratio = kRatioInfinite;
        q.attackMs = 0.5f;
        q.lookaheadMs = lookahead;
        Rig r(q);
        auto s = square(4000.0, 0.1, 0.0f);
        const auto hit = square(4000.0, 0.1, fromDb(-6.0f));
        s.insert(s.end(), hit.begin(), hit.end());
        const auto out = r.mono(s);
        const size_t at = static_cast<size_t>(0.1 * kFs) + static_cast<size_t>(r.comp.latencySamples());
        return toDb(peak(out, at, at + 48));
    };
    MESSAGE("first millisecond: " << hitPeak(0.0f) << " dB without lookahead, " << hitPeak(5.0f) << " dB with 5 ms");
    CHECK(hitPeak(5.0f) < -27.0);
    CHECK(hitPeak(0.0f) > -12.0);
}

TEST_CASE("the external sidechain drives the gain, and only when it is chosen and connected") {
    auto p = base();
    const auto main = sine(1000.0, 0.8, fromDb(-30.0f));
    const auto key = sine(100.0, 0.8, fromDb(-6.0f));
    auto levelWith = [&](const Params& q, bool connect) {
        Rig rig(q);
        Signal l = main, r = main;
        rig.run(l, r, connect ? &key : nullptr, connect ? &key : nullptr);
        return toDb(rms(l, l.size() * 3 / 4, l.size()) / rms(main, main.size() * 3 / 4, main.size()));
    };
    auto ext = p;
    ext.external = true;
    CHECK(std::abs(levelWith(ext, true) - (-10.5)) < 0.3);  // the key is 14 dB over the threshold at 4:1
    CHECK(std::abs(levelWith(ext, false)) < 0.05);          // chosen, but nothing connected: the main input is below
    CHECK(std::abs(levelWith(p, true)) < 0.05);             // connected, but not chosen
    // The key high-pass takes a low key out of the detector.
    ext.keyHighPassHz = 400.0f;
    auto lowKey = sine(50.0, 0.8, fromDb(-6.0f));
    {
        Rig rig(ext);
        Signal l = main, r = main;
        rig.run(l, r, &lowKey, &lowKey);
        CHECK(rig.comp.currentGainDb(0) > -0.5f);
    }
    // The tilt: +12 dB makes a 10 kHz key 6 dB louder to the detector and a 100 Hz key 6 dB quieter.
    auto tilted = base();
    tilted.keyTiltDb = 12.0f;
    tilted.ratio = kRatioInfinite;
    tilted.thresholdDb = -30.0f;
    auto heardGain = [&](double hz) {
        Rig rig(tilted);
        rig.mono(sine(hz, 0.5, fromDb(-12.0f)));
        return rig.comp.currentGainDb(0);
    };
    CHECK(heardGain(10000.0) == doctest::Approx(-23.6f).epsilon(0.04));
    CHECK(heardGain(100.0) == doctest::Approx(-12.0f).epsilon(0.06));
}

TEST_CASE("listening to the sidechain plays what the detector hears") {
    auto p = base();
    p.external = true;
    p.listen = true;
    const auto main = sine(1000.0, 0.3, 0.3f);
    const auto key = sine(220.0, 0.3, 0.5f);
    Rig rig(p);
    Signal l = main, r = main;
    rig.run(l, r, &key, &key);
    // The key comes out at its own level (the 20 Hz high-pass only turns its phase a little) and the main input is gone.
    const size_t a = l.size() / 2, b = l.size();
    CHECK(std::abs(toDb(levelAt(l, 220.0, a, b) / levelAt(key, 220.0, a, b))) < 0.1);
    CHECK(toDb(levelAt(l, 1000.0, a, b) / levelAt(main, 1000.0, a, b)) < -80.0);
}

TEST_CASE("dry/wet mix is a straight blend, makeup and output are exact") {
    auto p = base();
    p.thresholdDb = -30.0f;
    p.ratio = kRatioInfinite;
    p.mix = 0.0f;
    const auto in = sine(1000.0, 0.5, 0.5f);
    {
        Rig rig(p);
        const auto out = rig.mono(in);
        for (size_t i = 0; i < in.size(); ++i) REQUIRE(out[i] == in[i]);
    }
    p.mix = 0.5f;
    {
        Rig rig(p);
        const auto out = rig.mono(in);
        // Wet sits on -30 dBFS, the dry half at -6: half of each.
        const double expected = 0.5 * (0.5 + std::pow(10.0, -1.5));
        CHECK(peak(out, in.size() / 2, in.size()) == doctest::Approx(expected).epsilon(0.02));
    }
    auto m = base();
    m.makeupDb = 6.0f;
    m.outputDb = -2.0f;
    CHECK(std::abs(steadyGainDb(m, -30.0f) - 4.0) < 0.02);
    m.makeupDb = 0.0f;
    m.outputDb = 0.0f;
    m.autoMakeup = true;
    // Automatic makeup: half of what -10 dBFS loses; 10 dB over at 4:1 is 7,5 dB, so +3,75 dB.
    CHECK(std::abs(steadyGainDb(m, -40.0f) - 3.75) < 0.05);
    CHECK(autoMakeupDb(-20.0f, -0.75f, 0.0f, kNoLimitDb) == doctest::Approx(3.75f));
    CHECK(autoMakeupDb(-6.0f, -0.75f, 0.0f, kNoLimitDb) == 0.0f); // a threshold above the reference asks for nothing
}

TEST_CASE("stereo link: fully linked both sides duck together, unlinked each side on its own") {
    for (float link : { 0.0f, 1.0f }) {
        auto p = base();
        p.link = link;
        Rig rig(p);
        Signal l = sine(1000.0, 0.5, fromDb(-4.0f)), r = sine(1000.0, 0.5, fromDb(-40.0f));
        rig.run(l, r);
        CAPTURE(link);
        CHECK(rig.comp.currentGainDb(0) == doctest::Approx(-12.0f).epsilon(0.03));
        if (link > 0.5f) CHECK(rig.comp.currentGainDb(1) == doctest::Approx(-12.0f).epsilon(0.03));
        else CHECK(std::abs(rig.comp.currentGainDb(1)) < 0.01f);
    }
}

TEST_CASE("the half-band filters are flat in the audio band and the clip aliases far less than a plain one") {
    Oversampler os;
    os.prepare();
    for (double hz : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0 }) {
        const auto x = sine(hz, 0.25, 0.5f);
        Signal y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = os.passThrough(x[i]);
        CAPTURE(hz);
        CHECK(std::abs(toDb(levelAt(y, hz, x.size() / 2, x.size()) / levelAt(x, hz, x.size() / 2, x.size()))) < 0.05);
    }
    // What lands above the passband of the outer stage and folds back: a 30 kHz tone made at 96 kHz would alias to
    // 18 kHz. Clip a loud 7 kHz sine: its fifth harmonic, 35 kHz, folds to 13 kHz without oversampling.
    const auto x = sine(7000.0, 0.5, 2.0f);
    Signal naive(x.size()), over(x.size());
    float pk = 0.0f;
    os.reset();
    for (size_t i = 0; i < x.size(); ++i) {
        naive[i] = softClip(x[i]);
        over[i] = os.process(x[i], pk);
    }
    const size_t a = x.size() / 2, b = x.size();
    const double naiveAlias = toDb(levelAt(naive, 13000.0, a, b)), overAlias = toDb(levelAt(over, 13000.0, a, b));
    const double fundamental = toDb(levelAt(over, 7000.0, a, b));
    MESSAGE("alias at 13 kHz: " << naiveAlias << " dB plain, " << overAlias << " dB oversampled, fundamental " << fundamental);
    CHECK(overAlias < naiveAlias - 30.0);
    CHECK(overAlias < fundamental - 80.0);
    CHECK(pk > 1.9f); // a 7 kHz sine clipped this hard has a fundamental above full scale, so no peak check here
}

TEST_CASE("the soft clip leaves quiet signal alone and catches overs") {
    auto p = base();
    p.ratio = 1.0f;
    p.clip = true;
    {
        Rig rig(p);
        const auto in = sine(1000.0, 0.5, fromDb(-6.0f));
        const auto out = rig.mono(in);
        CHECK(std::abs(toDb(rms(out, in.size() / 2, in.size()) / rms(in, in.size() / 2, in.size()))) < 0.02);
    }
    {
        p.outputDb = 9.0f;
        Rig rig(p);
        const auto out = rig.mono(sine(1000.0, 0.5, 0.9f));
        CHECK(peak(out, out.size() / 2, out.size()) < 1.06);
        CHECK(rig.comp.meters.clipDb.load() > 6.0f);
    }
}

TEST_CASE("moving every control while it plays does not click") {
    const auto in = sine(150.0, 6.0, 0.4f);
    Rng rng(7);
    Compressor comp;
    comp.prepare(kFs);
    Params p = base();
    comp.setParams(p);
    comp.reset();
    Signal l = in, r = in;
    const auto key = sine(90.0, 6.0, 0.5f);
    double worst = 0.0;
    const size_t block = 480;
    for (size_t at = 0; at + block <= in.size(); at += block) {
        // Every few blocks jump all the controls to somewhere new.
        if ((at / block) % 5 == 0) {
            p.thresholdDb = -50.0f + 50.0f * rng.unit();
            p.ratio = rng.unit() < 0.1f ? kRatioInfinite : 0.5f + 20.0f * rng.unit();
            p.kneeDb = 24.0f * rng.unit();
            p.attackMs = 0.05f + 250.0f * rng.unit() * rng.unit();
            p.releaseMs = 5.0f + 2500.0f * rng.unit() * rng.unit();
            p.autoRelease = rng.unit() < 0.5f;
            p.rangeDb = 1.0f + 60.0f * rng.unit();
            p.makeupDb = -6.0f + 12.0f * rng.unit();
            p.autoMakeup = rng.unit() < 0.3f;
            p.mix = rng.unit();
            p.outputDb = -12.0f + 12.0f * rng.unit();
            p.rms = rng.unit();
            p.character = static_cast<Character>(static_cast<int>(rng.unit() * 2.99f));
            p.link = rng.unit();
            p.lookaheadMs = rng.unit() < 0.5f ? 0.0f : 10.0f * rng.unit();
            p.keyHighPassHz = 20.0f + 480.0f * rng.unit();
            p.keyTiltDb = -6.0f + 12.0f * rng.unit();
            p.external = rng.unit() < 0.5f;
            p.clip = rng.unit() < 0.5f;
            p.listen = false;
            comp.setParams(p);
        }
        comp.process(l.data() + at, r.data() + at, key.data() + at, key.data() + at, static_cast<int>(block));
    }
    // The second difference of a 150 Hz sine at this level stays tiny; a step of the gain would stand out.
    for (size_t i = 2; i < l.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(l[i] - 2.0f * l[i - 1] + l[i - 2])));
    MESSAGE("largest second difference: " << worst);
    CHECK(worst < 0.02);
    // The same measure sees a click: a gain step of 1 dB on this sine.
    Signal stepped = in;
    size_t top = stepped.size() / 2;
    while (std::abs(stepped[top]) < 0.39f) ++top;
    for (size_t i = top; i < stepped.size(); ++i) stepped[i] *= 0.89f;
    double click = 0.0;
    for (size_t i = 2; i < stepped.size(); ++i) click = std::max(click, static_cast<double>(std::abs(stepped[i] - 2.0f * stepped[i - 1] + stepped[i - 2])));
    CHECK(click > 0.02);
}

TEST_CASE("wild settings stay finite at every sample rate") {
    Rng rng(99);
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (int trial = 0; trial < 12; ++trial) {
            Params p;
            p.thresholdDb = -60.0f * rng.unit();
            p.ratio = rng.unit() < 0.3f ? kRatioInfinite : (rng.unit() < 0.2f ? 0.5f : 1.0f + 99.0f * rng.unit());
            p.kneeDb = rng.unit() < 0.3f ? 0.0f : 24.0f * rng.unit();
            p.attackMs = rng.unit() < 0.3f ? 0.05f : 250.0f * rng.unit();
            p.releaseMs = rng.unit() < 0.3f ? 5.0f : 2500.0f * rng.unit();
            p.autoRelease = rng.unit() < 0.5f;
            p.rangeDb = 60.0f * rng.unit();
            p.makeupDb = -12.0f + 36.0f * rng.unit();
            p.autoMakeup = rng.unit() < 0.5f;
            p.mix = rng.unit();
            p.outputDb = -24.0f + 36.0f * rng.unit();
            p.rms = rng.unit();
            p.character = static_cast<Character>(trial % 3);
            p.link = rng.unit();
            p.lookaheadMs = 10.0f * rng.unit();
            p.keyHighPassHz = 20.0f + 480.0f * rng.unit();
            p.keyTiltDb = -6.0f + 12.0f * rng.unit();
            p.external = rng.unit() < 0.5f;
            p.listen = rng.unit() < 0.2f;
            p.clip = rng.unit() < 0.5f;
            Compressor comp;
            comp.prepare(fs);
            comp.setParams(p);
            Signal l(static_cast<size_t>(fs * 0.25)), r(l.size()), k(l.size());
            for (size_t i = 0; i < l.size(); ++i) {
                const float burst = (i / 2000) % 2 == 0 ? 1.0f : 0.001f;
                l[i] = burst * 4.0f * rng.next();
                r[i] = (i % 997 == 0) ? 8.0f : burst * rng.next();
                k[i] = rng.next() * 3.0f;
            }
            comp.process(l.data(), r.data(), k.data(), k.data(), static_cast<int>(l.size()));
            bool finite = true;
            for (size_t i = 0; i < l.size(); ++i) finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            CAPTURE(fs);
            CAPTURE(trial);
            CHECK(finite);
            CHECK(std::isfinite(comp.currentGainDb(0)));
        }
    }
}

TEST_CASE("processing allocates nothing, even while every control changes") {
    Compressor comp;
    comp.prepare(kFs);
    Params p;
    comp.setParams(p);
    Signal l = sine(300.0, 0.5, 0.8f), r = l, dl(l.size()), dr(l.size());
    Rng rng(3);
    wers::test::AllocationCounter counter;
    for (size_t at = 0; at + 512 <= l.size(); at += 512) {
        p.thresholdDb = -40.0f * rng.unit();
        p.lookaheadMs = 10.0f * rng.unit();
        p.character = static_cast<Character>(static_cast<int>(rng.unit() * 2.99f));
        p.clip = rng.unit() < 0.5f;
        comp.setParams(p);
        comp.process(l.data() + at, r.data() + at, l.data() + at, r.data() + at, 512, dl.data() + at, dr.data() + at);
    }
    CHECK(counter.stop() == 0);
}

TEST_CASE("meters and the history see what happens") {
    auto p = base();
    p.thresholdDb = -30.0f;
    p.ratio = kRatioInfinite;
    Rig rig(p);
    rig.mono(sine(1000.0, 1.0, fromDb(-6.0f)));
    auto& m = rig.comp.meters;
    CHECK(Meters::take(m.inPeak[0]) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(gainToDb(Meters::take(m.outPeak[0])) < -5.0f); // the first moments before the attack catch up
    CHECK(m.gainDb.load() == doctest::Approx(-24.0f).epsilon(0.02));
    CHECK(m.levelDb.load() > -7.0f);
    std::vector<Frame> frames(1000);
    uint32_t cursor = 0;
    const int n = rig.comp.history.read(cursor, frames.data(), 1000);
    CHECK(n == 200); // one second in 5 ms frames
    CHECK(gainToDb(frames[static_cast<size_t>(n - 1)].out) == doctest::Approx(-30.0f).epsilon(0.02));
    CHECK(frames[static_cast<size_t>(n - 1)].gain == doctest::Approx(-24.0f).epsilon(0.02));
    CHECK(rig.comp.history.read(cursor, frames.data(), 1000) == 0);
}

TEST_CASE("bypass support: the delayed dry signal comes out in time with the output") {
    auto p = base();
    p.lookaheadMs = 3.0f;
    Rig rig(p);
    Signal l = sine(500.0, 0.2, 0.3f), r = l, dl(l.size()), dr(l.size());
    const auto in = l;
    rig.comp.process(l.data(), r.data(), nullptr, nullptr, static_cast<int>(l.size()), dl.data(), dr.data());
    const size_t d = static_cast<size_t>(rig.comp.latencySamples());
    CHECK(d == 144);
    for (size_t i = d; i < in.size(); i += 97) CHECK(dl[i] == in[i - d]);
}

TEST_CASE("changing the lookahead while it plays glides and reports the new latency") {
    auto p = base();
    Rig rig(p);
    Signal l = sine(200.0, 1.0, 0.3f), r = l;
    rig.comp.process(l.data(), r.data(), nullptr, nullptr, 24000);
    p.lookaheadMs = 8.0f;
    rig.comp.setParams(p);
    CHECK(rig.comp.latencySamples() == 384);
    rig.comp.process(l.data() + 24000, r.data() + 24000, nullptr, nullptr, 24000);
    double worst = 0.0;
    for (size_t i = 2; i < l.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(l[i] - 2.0f * l[i - 1] + l[i - 2])));
    CHECK(worst < 0.01);
}

#include "TestAudio.h"
TEST_CASE("upward expansion raises music in every character, never lowers it") {
    const auto music = wir::test::demoLoop(kFs, 3.0);
    for (int c = 0; c < 3; ++c) {
        Params p;
        p.thresholdDb = -26.0f;
        p.ratio = 0.7f;
        p.kneeDb = 6.0f;
        p.attackMs = 2.0f;
        p.releaseMs = 70.0f;
        p.rangeDb = 6.0f;
        p.character = static_cast<Character>(c);
        Compressor comp;
        comp.prepare(kFs);
        comp.setParams(p);
        comp.reset();
        Signal l = music.l, r = music.r;
        float lowest = 0.0f, highest = 0.0f;
        for (size_t at = 0; at + 256 <= l.size(); at += 256) {
            comp.process(l.data() + at, r.data() + at, nullptr, nullptr, 256);
            lowest = std::min(lowest, comp.currentGainDb(0));
            highest = std::max(highest, comp.currentGainDb(0));
        }
        CAPTURE(c);
        CHECK(lowest > -0.01f);
        CHECK(highest > 2.0f);
        CHECK(highest < 6.01f);
    }
}
