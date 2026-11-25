// Tests of the Lampa engine on its own, without JUCE: every claim measured on a signal.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "AllocationCount.h"
#include "Lampa.h"
#include "Needle.h"
#include <cmath>
#include <cstdio>
#include <vector>

WERS_COUNT_ALLOCATIONS()

using namespace lampa;

namespace {

constexpr double kFs = 48000.0;

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed * 2654435761u + 1u) {}
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) * (2.0f / 4294967296.0f) - 1.0f;
    }
    float unit() { return 0.5f + 0.5f * next(); }
};

float fromDb(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }
double toDb(double x) { return 20.0 * std::log10(std::max(1e-15, x)); }

std::vector<float> sine(double hz, double seconds, float amp, double fs = kFs, double phase = 0.0) {
    std::vector<float> v(static_cast<size_t>(fs * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs + phase));
    return v;
}

// A plain setup: no compression, no colour, so tests change only what they look at.
Params clean() {
    Params p;
    p.squeeze = 0.0f;
    p.gainDb = 0.0f;
    p.color = 0.0f;
    return p;
}

// Runs both channels through the engine in uneven blocks, calling `each` after every block.
template <class F>
void run(Engine& e, std::vector<float>& l, std::vector<float>& r, const Params& p, F&& each) {
    size_t at = 0;
    int k = 0;
    while (at < l.size()) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(97 + (k++ % 5) * 61), l.size() - at));
        e.setParams(p);
        e.process(l.data() + at, r.data() + at, n);
        at += static_cast<size_t>(n);
        each(at);
    }
}

void run(Engine& e, std::vector<float>& l, std::vector<float>& r, const Params& p) {
    run(e, l, r, p, [](size_t) {});
}

// Amplitude of the bin `bin` of the last `n` samples, by Goertzel (exact for a whole number of periods).
double binAmp(const std::vector<float>& x, size_t n, size_t bin) {
    const size_t from = x.size() - n;
    const double w = 2.0 * kPi * static_cast<double>(bin) / static_cast<double>(n), c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < x.size(); ++i) {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

double rms(const std::vector<float>& x, size_t from, size_t to) {
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(std::max<size_t>(1, to - from)));
}

// A tone whose frequency sits exactly on a bin of an N-point analysis, so harmonics and aliases do too.
constexpr size_t kN = 16384;
double binHz(size_t bin, double fs = kFs) { return static_cast<double>(bin) * fs / static_cast<double>(kN); }

struct Spectrum {
    double fundamental = 0.0;
    std::vector<double> harmonics; // amplitude of harmonic k at [k], relative to the fundamental
    double thdDb = -200.0;         // all in-band harmonics
    double residueDb = -200.0;     // everything that is neither DC, the tone nor an in-band harmonic: aliasing and noise
};

Spectrum analyse(const std::vector<float>& x, size_t bin, double fs = kFs) {
    Spectrum s;
    s.fundamental = binAmp(x, kN, bin);
    s.harmonics.assign(12, 0.0);
    double total = 0.0, mean = 0.0;
    const size_t from = x.size() - kN;
    for (size_t i = from; i < x.size(); ++i) mean += x[i];
    mean /= static_cast<double>(kN);
    for (size_t i = from; i < x.size(); ++i) total += (x[i] - mean) * (x[i] - mean);
    total /= static_cast<double>(kN);
    double known = s.fundamental * s.fundamental * 0.5, harm = 0.0;
    for (size_t k = 2; k * bin < kN / 2; ++k) {
        const double a = binAmp(x, kN, k * bin);
        if (k < s.harmonics.size()) s.harmonics[k] = a / s.fundamental;
        harm += a * a * 0.5;
    }
    known += harm;
    (void)fs;
    s.thdDb = 10.0 * std::log10(std::max(1e-30, harm / (s.fundamental * s.fundamental * 0.5)));
    s.residueDb = 10.0 * std::log10(std::max(1e-30, (total - known) / (s.fundamental * s.fundamental * 0.5)));
    return s;
}

// Plays a bin-centred tone through settings and analyses the settled output of the left channel.
Spectrum toneThrough(const Params& p, size_t bin, double amplitudeDb, double fs = kFs) {
    Engine e;
    e.prepare(fs);
    const double seconds = 1.2 + static_cast<double>(kN) / fs;
    auto l = sine(binHz(bin, fs), seconds, fromDb(amplitudeDb), fs), r = l;
    run(e, l, r, p);
    return analyse(l, bin, fs);
}

// Steady reduction for a 1 kHz sine at a level, after two seconds.
float steadyReduction(Params p, double levelDb, int channel = 0) {
    Engine e;
    e.prepare(kFs);
    auto l = sine(1000.0, 2.0, fromDb(levelDb)), r = l;
    run(e, l, r, p);
    return e.reductionDb(channel);
}

double peakAbs(const std::vector<float>& x, size_t from = 0) {
    float m = 0.0f;
    for (size_t i = from; i < x.size(); ++i) m = std::max(m, std::abs(x[i]));
    return m;
}

} // namespace


// --- Oversampling ---------------------------------------------------------------------------------

TEST_CASE("the oversampler is flat across the audio band and delays by whole samples") {
    for (int factor : { 2, 4, 8 }) {
        CAPTURE(factor);
        Oversampler os;
        os.setFactor(factor);
        const int d = os.latency();
        CHECK(d > 0);
        CHECK(d < Engine::kMaxLatency);
        for (double hz : { 50.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0 }) {
            CAPTURE(hz);
            const auto in = sine(hz, 0.25, 0.5f);
            std::vector<float> out(in.size());
            os.reset();
            for (size_t i = 0; i < in.size(); ++i) out[i] = os.process(in[i], [](float v) { return v; });
            // Output is the input, d samples later: no phase error, no level change.
            double err = 0.0, ref = 0.0;
            for (size_t i = 2000; i < in.size(); ++i) {
                const double e = out[i] - in[i - static_cast<size_t>(d)];
                err += e * e;
                ref += static_cast<double>(in[i]) * in[i];
            }
            CHECK(10.0 * std::log10(err / ref) < -60.0);
        }
    }
}

TEST_CASE("the oversampler lets no images through") {
    // A 15 kHz tone through the identity: nothing but the tone comes out.
    Oversampler os;
    os.setFactor(4);
    const size_t bin = 5120; // 15 kHz
    auto x = sine(binHz(bin), 0.3 + static_cast<double>(kN) / kFs, 0.9f);
    for (auto& v : x) v = os.process(v, [](float s) { return s; });
    const auto s = analyse(x, bin);
    MESSAGE("identity residue " << s.residueDb << " dB");
    CHECK(s.residueDb < -100.0);
}

// --- Gain reduction ---------------------------------------------------------------------------------

TEST_CASE("gain reduction against level: nothing below the knee, then more and more, harder as it goes") {
    struct Mode {
        const char* name;
        Character character;
        bool limit;
        double minFinalSlope, maxFinalSlope;
    };
    const Mode modes[] = { { "opto", Character::Opto, false, 0.55, 0.80 },
                           { "opto limit", Character::Opto, true, 0.88, 1.0 },
                           { "tube", Character::Tube, false, 0.50, 0.80 },
                           { "tube limit", Character::Tube, true, 0.80, 1.0 } };
    for (const auto& m : modes) {
        CAPTURE(m.name);
        Params p = clean();
        p.squeeze = 48.6f; // threshold -21,2 dBFS
        p.character = m.character;
        p.limit = m.limit;
        std::vector<float> gr;
        for (int db = -50; db <= 0; db += 2) gr.push_back(steadyReduction(p, db));
        CHECK(gr.front() < 0.05f);
        for (size_t i = 1; i < gr.size(); ++i) CHECK(gr[i] >= gr[i - 1] - 0.02f);
        // Slope in dB of reduction per dB of input, low in the knee and steeper above it.
        const double kneeSlope = (gr[14] - gr[12]) / 4.0;   // -22 .. -26 dBFS: around the threshold
        const double finalSlope = (gr[25] - gr[23]) / 4.0;  // -4 .. 0 dBFS
        MESSAGE(m.name << ": at threshold " << gr[14] << " dB, at 0 dBFS " << gr.back() << " dB, slopes " << kneeSlope << " / " << finalSlope);
        CHECK(finalSlope > kneeSlope + 0.1);
        CHECK(finalSlope > m.minFinalSlope);
        CHECK(finalSlope < m.maxFinalSlope);
        CHECK(gr.back() > 10.0f);
    }
    // The tube's knee is wider: it starts working earlier and more gently than the optical one.
    Params o = clean(), t = clean();
    o.squeeze = t.squeeze = 48.6f;
    t.character = Character::Tube;
    CHECK(steadyReduction(t, -28.0) > steadyReduction(o, -28.0) + 0.15f);
    // Limit squeezes harder than leveling.
    Params lim = o;
    lim.limit = true;
    CHECK(steadyReduction(lim, -6.0) > steadyReduction(o, -6.0) + 3.0f);
    // More Ściskanie, more reduction at the same level.
    Params more = o;
    more.squeeze = 80.0f;
    CHECK(steadyReduction(more, -12.0) > steadyReduction(o, -12.0) + 4.0f);
}

TEST_CASE("the output follows the reduction and the gain") {
    Params p = clean();
    p.squeeze = 48.6f;
    Engine e;
    e.prepare(kFs);
    auto l = sine(1000.0, 2.0, fromDb(-6.0)), r = l;
    run(e, l, r, p);
    const double outDb = toDb(rms(l, l.size() - 9600, l.size()) * std::sqrt(2.0));
    CHECK(outDb == doctest::Approx(-6.0 - e.reductionDb(0)).epsilon(0.03));
    // Wzmocnienie adds what it says, the output knob too.
    p.gainDb = 6.0f;
    p.outputGain = fromDb(-3.0);
    Engine e2;
    e2.prepare(kFs);
    auto l2 = sine(1000.0, 2.0, fromDb(-6.0)), r2 = l2;
    run(e2, l2, r2, p);
    const double out2 = toDb(rms(l2, l2.size() - 9600, l2.size()) * std::sqrt(2.0));
    CHECK(out2 - outDb == doctest::Approx(3.0).epsilon(0.05));
}

TEST_CASE("attack depends on the program: a bigger jump is caught faster") {
    auto timeTo63 = [](double stepDb) {
        Params p = clean();
        p.squeeze = 48.6f;
        Engine e;
        e.prepare(kFs);
        auto quiet = sine(1000.0, 0.5, fromDb(-40.0)), r = quiet;
        run(e, quiet, r, p);
        const float target = steadyReduction(p, -21.2 + stepDb);
        auto loud = sine(1000.0, 0.3, fromDb(-21.2 + stepDb)), r2 = loud;
        double hit = -1.0;
        run(e, loud, r2, p, [&](size_t at) {
            if (hit < 0.0 && e.reductionDb(0) >= 0.632f * target) hit = static_cast<double>(at) / kFs;
        });
        return hit;
    };
    const double small = timeTo63(8.0), big = timeTo63(24.0);
    MESSAGE("63% of the reduction after " << small * 1000.0 << " ms for a small jump, " << big * 1000.0 << " ms for a big one");
    CHECK(small > 0.0);
    CHECK(big > 0.0);
    CHECK(big < 0.7 * small);
    CHECK(small < 0.030);
}

TEST_CASE("release depends on the program: longer and harder compression leaves a longer tail") {
    // Ten dB of reduction for a moment, or for three seconds, then silence: how long until it is back?
    auto recover = [](Character character, double seconds, double* afterFast, double* afterSecond) {
        Params p = clean();
        p.squeeze = 48.6f;
        p.character = character;
        Engine e;
        e.prepare(kFs);
        auto loud = sine(1000.0, seconds, fromDb(-5.0)), r = loud;
        run(e, loud, r, p);
        const double start = e.reductionDb(0);
        std::vector<float> silence(static_cast<size_t>(kFs * 12.0), 0.0f), r2 = silence;
        double back = -1.0;
        run(e, silence, r2, p, [&](size_t at) {
            const double t = static_cast<double>(at) / kFs;
            if (afterFast != nullptr && t <= 0.15) *afterFast = e.reductionDb(0) / start;
            if (afterSecond != nullptr && t <= 1.0) *afterSecond = e.reductionDb(0);
            if (back < 0.0 && e.reductionDb(0) < 1.0f) back = t;
        });
        return back;
    };
    double fastShare = 0.0, tail = 0.0;
    const double shortOpto = recover(Character::Opto, 0.1, nullptr, nullptr);
    const double longOpto = recover(Character::Opto, 3.0, &fastShare, &tail);
    MESSAGE("opto back under 1 dB after " << shortOpto << " s (short burst) and " << longOpto << " s (three seconds)");
    MESSAGE("opto after 150 ms holds " << fastShare * 100.0 << "% of the reduction, after 1 s " << tail << " dB");
    CHECK(shortOpto > 0.0);
    CHECK(shortOpto < 0.4);
    CHECK(longOpto > 4.0 * shortOpto);
    CHECK(longOpto > 1.5);
    // Two stages: the first part falls quickly, the memory lingers.
    CHECK(fastShare < 0.8);
    CHECK(tail > 2.0);

    const double shortTube = recover(Character::Tube, 0.1, nullptr, nullptr);
    const double longTube = recover(Character::Tube, 3.0, nullptr, nullptr);
    MESSAGE("tube back under 1 dB after " << shortTube << " s and " << longTube << " s");
    CHECK(longTube > 1.5 * shortTube);
    // The tube's first release is slower than the optical one: a smoother, glued feel.
    CHECK(shortTube > shortOpto);

    // Harder compression for the same time also leaves a longer tail.
    auto hardness = [](double levelDb) {
        Params p = clean();
        p.squeeze = 48.6f;
        Engine e;
        e.prepare(kFs);
        auto loud = sine(1000.0, 2.0, fromDb(levelDb)), r = loud;
        run(e, loud, r, p);
        std::vector<float> silence(static_cast<size_t>(kFs * 1.0), 0.0f), r2 = silence;
        run(e, silence, r2, p);
        return e.memoryDb(0);
    };
    CHECK(hardness(0.0) > 1.5 * hardness(-12.0));
}

TEST_CASE("the side-chain filter keeps the bass from pumping") {
    Params p = clean();
    p.squeeze = 48.6f;
    const float off = steadyReduction(p, -6.0);
    p.sidechainHz = 200.0f;
    Engine e;
    e.prepare(kFs);
    auto l = sine(50.0, 2.0, fromDb(-6.0)), r = l;
    Params q = p;
    q.sidechainHz = 20.0f;
    Engine e0;
    e0.prepare(kFs);
    auto l0 = l, r0 = r;
    run(e0, l0, r0, q);
    run(e, l, r, p);
    MESSAGE("50 Hz at -6 dBFS: " << e0.reductionDb(0) << " dB reduction without the filter, " << e.reductionDb(0) << " dB with it at 200 Hz");
    CHECK(e0.reductionDb(0) > 0.8f * off);
    CHECK(e.reductionDb(0) < 0.3f * e0.reductionDb(0));
    // A 2 kHz tone does not care about the filter.
    p.sidechainHz = 200.0f;
    CHECK(steadyReduction(p, -6.0) == doctest::Approx(off).epsilon(0.05));
}

TEST_CASE("stereo link: linked channels move together, unlinked ones on their own") {
    auto rightReduction = [](float link) {
        Params p = clean();
        p.squeeze = 48.6f;
        p.link = link;
        Engine e;
        e.prepare(kFs);
        auto l = sine(1000.0, 1.5, fromDb(-6.0)), r = sine(700.0, 1.5, fromDb(-40.0));
        run(e, l, r, p);
        return std::pair<float, float>(e.reductionDb(0), e.reductionDb(1));
    };
    const auto linked = rightReduction(1.0f), free = rightReduction(0.0f), half = rightReduction(0.5f);
    CHECK(linked.second == doctest::Approx(linked.first).epsilon(0.01));
    CHECK(free.second < 0.05f);
    CHECK(free.first == doctest::Approx(linked.first).epsilon(0.02));
    CHECK(half.second > 1.0f);
    CHECK(half.second < linked.second - 1.0f);
}

// --- Colour ---------------------------------------------------------------------------------------

TEST_CASE("Kolor at zero is clean; more Kolor, more harmonics") {
    const size_t bin = 341; // 999 Hz
    double last = -200.0;
    for (float color : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        CAPTURE(color);
        Params p = clean();
        p.color = color;
        const auto s = toneThrough(p, bin, -12.0);
        MESSAGE("Kolor " << color * 100.0f << "%: THD " << s.thdDb << " dB, H2 " << toDb(s.harmonics[2]) << " dB, H3 " << toDb(s.harmonics[3]) << " dB");
        if (color == 0.0f) CHECK(s.thdDb < -60.0);
        CHECK(s.thdDb > last + 4.0);
        last = s.thdDb;
    }
    CHECK(last > -30.0);
    CHECK(last < -12.0);
}

TEST_CASE("the optical side colours with even harmonics, the tube side with odd ones") {
    const size_t bin = 341;
    Params p = clean();
    p.color = 0.5f;
    const auto opto = toneThrough(p, bin, -12.0);
    p.character = Character::Tube;
    const auto tube = toneThrough(p, bin, -12.0);
    MESSAGE("opto H2 " << toDb(opto.harmonics[2]) << " H3 " << toDb(opto.harmonics[3]) << "; tube H2 " << toDb(tube.harmonics[2]) << " H3 " << toDb(tube.harmonics[3]));
    CHECK(opto.harmonics[2] > 3.0 * opto.harmonics[3]);
    CHECK(tube.harmonics[3] > 1.5 * tube.harmonics[2]);
}

TEST_CASE("the harmonics depend on the level: louder means more, and odd ones grow faster") {
    const size_t bin = 341;
    Params p = clean();
    p.color = 0.6f;
    const auto quiet = toneThrough(p, bin, -30.0), mid = toneThrough(p, bin, -18.0), loud = toneThrough(p, bin, -6.0);
    MESSAGE("THD at -30 / -18 / -6 dBFS: " << quiet.thdDb << " / " << mid.thdDb << " / " << loud.thdDb << " dB");
    CHECK(mid.thdDb > quiet.thdDb + 8.0);
    CHECK(loud.thdDb > mid.thdDb + 8.0);
    const double ratioQuiet = quiet.harmonics[3] / quiet.harmonics[2], ratioLoud = loud.harmonics[3] / loud.harmonics[2];
    CHECK(ratioLoud > 4.0 * ratioQuiet);
}

TEST_CASE("the tube is driven harder by its own compression") {
    // The same tube input level once without and once with 10 dB of reduction in front of it.
    TubeStage a, b;
    const auto shape = TubeShape::make(0.5f, 1.0f, 192000.0);
    auto thd = [&](TubeStage& t, float drive) {
        std::vector<float> x(static_cast<size_t>(kN) * 4);
        for (size_t i = 0; i < x.size(); ++i) x[i] = t.process(0.3f * static_cast<float>(std::sin(2.0 * kPi * 341.0 * static_cast<double>(i) / kN)), shape, drive);
        return analyse(x, 341).thdDb;
    };
    const double plain = thd(a, 1.0f), squeezed = thd(b, 1.0f + shape.squeezeDrive * 10.0f);
    MESSAGE("tube THD " << plain << " dB plain, " << squeezed << " dB under 10 dB of reduction");
    CHECK(squeezed > plain + 3.0);
}

TEST_CASE("saturation does not alias") {
    // A loud 15 kHz tone at full Kolor: its harmonics are all above the band, so whatever shows up in the
    // band is aliasing (plus rounding noise).
    for (Character c : { Character::Opto, Character::Tube }) {
        Params p = clean();
        p.color = 1.0f;
        p.character = c;
        const auto s = toneThrough(p, 5120, -6.0);
        MESSAGE((c == Character::Opto ? "opto" : "tube") << ": aliasing " << s.residueDb << " dB below the tone at 15 kHz");
        CHECK(s.residueDb < -85.0);
        // At 7 kHz the second and third harmonics are in the band and are allowed; aliases are not.
        const auto t = toneThrough(p, 2389, -6.0); // 6999 Hz
        MESSAGE("  at 7 kHz: harmonics " << t.thdDb << " dB, aliasing " << t.residueDb << " dB");
        CHECK(t.thdDb > -40.0);
        CHECK(t.residueDb < -85.0);
    }
    // Also at 44,1 kHz, and at 96 kHz where it oversamples twice.
    for (double fs : { 44100.0, 96000.0 }) {
        CAPTURE(fs);
        Params p = clean();
        p.color = 1.0f;
        const size_t bin = static_cast<size_t>(15000.0 / fs * kN);
        CHECK(toneThrough(p, bin, -6.0, fs).residueDb < -80.0);
    }
}

TEST_CASE("the transformers add a little weight down low and soften the top, more with Kolor") {
    auto response = [](float color, double hz) {
        Params p = clean();
        p.color = color;
        Engine e;
        e.prepare(kFs);
        auto l = sine(hz, 1.5, fromDb(-40.0)), r = l;
        run(e, l, r, p);
        return toDb(rms(l, l.size() / 2, l.size()) / rms(sine(hz, 1.5, fromDb(-40.0)), 0, 48000));
    };
    for (float color : { 0.0f, 1.0f }) {
        CAPTURE(color);
        const double lo = response(color, 70.0), mid = response(color, 1000.0), hi = response(color, 16000.0);
        MESSAGE("Kolor " << color * 100.0f << "%: 70 Hz " << lo - mid << " dB, 16 kHz " << hi - mid << " dB against 1 kHz");
        CHECK(std::abs(mid) < 0.15);
        if (color == 0.0f) {
            CHECK(std::abs(lo - mid) < 0.5);
            CHECK(std::abs(hi - mid) < 0.5);
        } else {
            CHECK(lo - mid > 0.8);
            CHECK(lo - mid < 2.0);
            CHECK(hi - mid < -0.8);
            CHECK(hi - mid > -2.5);
        }
        CHECK(response(color, 20.0) - mid > -2.0); // the subsonic roll-off stays below the music
    }
}

// --- Mix, latency, bypass -----------------------------------------------------------------------------

TEST_CASE("Mix at zero and bypass give the input exactly, delayed by the reported latency") {
    Engine e;
    e.prepare(kFs);
    const int d = e.latency();
    CHECK(d == 52);
    Rng rng(5);
    std::vector<float> in(24000);
    for (auto& v : in) v = 0.5f * rng.next();
    for (int mode = 0; mode < 2; ++mode) {
        CAPTURE(mode);
        Params p;
        p.squeeze = 80.0f;
        p.color = 1.0f;
        p.gainDb = 12.0f;
        if (mode == 0) p.mix = 0.0f;
        else p.bypass = true;
        Engine x;
        x.prepare(kFs);
        auto l = in, r = in;
        run(x, l, r, p);
        float diff = 0.0f;
        for (size_t i = 2000; i < l.size(); ++i) diff = std::max(diff, std::abs(l[i] - in[i - static_cast<size_t>(d)]));
        CHECK(diff < 1e-6f);
    }
    // At full Mix with nothing to do, the processed signal lines up with the dry one: half and half sums.
    Params p = clean();
    p.mix = 1.0f;
    Engine x;
    x.prepare(kFs);
    auto l = sine(3000.0, 1.0, 0.25f), r = l;
    const auto dry = l;
    run(x, l, r, p);
    double err = 0.0, ref = 0.0;
    for (size_t i = 12000; i < l.size(); ++i) {
        const double e2 = l[i] - dry[i - static_cast<size_t>(d)];
        err += e2 * e2;
        ref += static_cast<double>(dry[i]) * dry[i];
    }
    CHECK(10.0 * std::log10(err / ref) < -30.0);
}

TEST_CASE("latency is a whole number at every sample rate, and lines up there too") {
    for (double fs : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 }) {
        CAPTURE(fs);
        Engine e;
        e.prepare(fs);
        const int d = e.latency();
        CHECK(d > 0);
        CHECK(d <= Engine::kMaxLatency);
        Params p = clean();
        auto l = sine(1000.0, 1.0, 0.25f, fs), r = l;
        const auto dry = l;
        run(e, l, r, p);
        double err = 0.0, ref = 0.0;
        for (size_t i = l.size() / 2; i < l.size(); ++i) {
            const double e2 = l[i] - dry[i - static_cast<size_t>(d)];
            err += e2 * e2;
            ref += static_cast<double>(dry[i]) * dry[i];
        }
        CHECK(10.0 * std::log10(err / ref) < -35.0);
    }
}

// --- Robustness ---------------------------------------------------------------------------------------

TEST_CASE("moving every control while it plays does not click") {
    // A sine with steady compression, while every control jumps around. The third difference of the output
    // stays as small as it is with the settings held still at their wildest.
    auto play = [](bool moving) {
        Engine e;
        e.prepare(kFs);
        auto l = sine(150.0, 4.0, 0.4f), r = sine(150.0, 4.0, 0.4f, kFs, 1.1);
        Params p;
        size_t at = 0;
        int block = 0;
        while (at < l.size()) {
            const int k = moving ? block : 1;
            p.squeeze = (k / 10) % 2 == 0 ? 10.0f : 90.0f;
            p.gainDb = (k / 7) % 2 == 0 ? 0.0f : 12.0f;
            p.color = (k / 9) % 2 == 0 ? 0.0f : 1.0f;
            p.mix = (k / 13) % 2 == 0 ? 1.0f : 0.3f;
            p.character = (k / 11) % 2 == 0 ? Character::Opto : Character::Tube;
            p.limit = (k / 17) % 2 == 1;
            p.link = (k / 19) % 2 == 0 ? 1.0f : 0.0f;
            p.sidechainHz = (k / 23) % 2 == 0 ? 20.0f : 400.0f;
            p.outputGain = (k / 29) % 2 == 0 ? 1.0f : 0.5f;
            p.bypass = moving && (block / 31) % 2 == 1;
            const int n = static_cast<int>(std::min<size_t>(256, l.size() - at));
            e.setParams(p);
            e.process(l.data() + at, r.data() + at, n);
            at += static_cast<size_t>(n);
            ++block;
        }
        double worst = 0.0;
        for (size_t i = 4800; i < l.size(); ++i)
            for (const auto* x : { &l, &r }) {
                const auto& v = *x;
                worst = std::max(worst, std::abs(static_cast<double>(v[i]) - 3.0 * v[i - 1] + 3.0 * v[i - 2] - v[i - 3]));
            }
        return worst;
    };
    const double still = play(false), moving = play(true);
    MESSAGE("largest third difference: " << still << " held still, " << moving << " while moving");
    CHECK(moving < 0.02);
    CHECK(moving < 3.0 * still + 0.002);
}

TEST_CASE("wild settings stay finite at every sample rate") {
    Rng rng(42);
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) {
        Engine e;
        e.prepare(fs);
        for (int round = 0; round < 25; ++round) {
            CAPTURE(fs);
            CAPTURE(round);
            Params p;
            p.squeeze = 100.0f * rng.unit();
            p.gainDb = 30.0f * rng.unit();
            p.color = rng.unit();
            p.mix = rng.unit();
            p.character = rng.unit() > 0.5f ? Character::Tube : Character::Opto;
            p.limit = rng.unit() > 0.5f;
            p.link = rng.unit();
            p.sidechainHz = 20.0f + 480.0f * rng.unit();
            p.outputGain = fromDb(-18.0 + 30.0 * rng.unit());
            p.bypass = rng.unit() > 0.8f;
            const size_t n = static_cast<size_t>(fs * 0.1);
            std::vector<float> l(n), r = sine(40.0 + round * 97.0, 0.1, 4.0f, fs);
            for (auto& v : l) v = 8.0f * rng.next();
            if (round % 5 == 0) std::fill(l.begin(), l.end(), 0.0f);
            run(e, l, r, p);
            for (size_t i = 0; i < n; ++i) {
                REQUIRE(std::isfinite(l[i]));
                REQUIRE(std::isfinite(r[i]));
            }
            CHECK(peakAbs(l) < 40.0);
        }
    }
}

TEST_CASE("processing allocates nothing") {
    Engine e;
    e.prepare(kFs);
    Rng rng(3);
    std::vector<float> l(9600), r(9600);
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = 0.5f * rng.next();
        r[i] = 0.5f * rng.next();
    }
    Params p;
    p.character = Character::Tube;
    p.limit = true;
    p.sidechainHz = 120.0f;
    wers::test::AllocationCounter counter;
    run(e, l, r, p);
    CHECK(counter.stop() == 0);
}

// --- Meter ----------------------------------------------------------------------------------------------

TEST_CASE("the meters read what the signal is, and the needle moves like a VU meter") {
    Params p = clean();
    p.squeeze = 48.6f;
    Engine e;
    e.prepare(kFs);
    // A sine at -18 dBFS RMS is 0 VU on the output meter.
    auto l = sine(1000.0, 1.0, fromDb(-15.0)), r = l;
    run(e, l, r, clean());
    CHECK(toDb(e.meterOutput()) == doctest::Approx(-18.0).epsilon(0.02));
    CHECK(toDb(e.meterInput()) == doctest::Approx(-18.0).epsilon(0.02));
    auto l2 = sine(1000.0, 2.0, fromDb(-3.0)), r2 = l2;
    run(e, l2, r2, p);
    CHECK(e.meterReductionDb() == doctest::Approx(e.reductionDb(0)).epsilon(0.01));
    CHECK(e.meterReductionDb() > 5.0f);
    CHECK(e.meterMemoryDb() > 0.5f);
    CHECK(e.meterMemoryDb() < e.meterReductionDb());

    // Step response of the needle: 99% in about 300 ms, a little overshoot.
    Needle n;
    double reached = -1.0, top = 0.0;
    for (int ms = 1; ms <= 1500; ++ms) {
        n.step(1.0f, 0.001f);
        if (reached < 0.0 && n.position >= 0.99f) reached = ms / 1000.0;
        top = std::max(top, static_cast<double>(n.position));
    }
    MESSAGE("needle: 99% after " << reached * 1000.0 << " ms, overshoot " << (top - 1.0) * 100.0 << "%");
    CHECK(reached > 0.25);
    CHECK(reached < 0.35);
    CHECK(top > 1.005);
    CHECK(top < 1.02);
    // A slow frame rate gives the same swing.
    Needle a, b;
    for (int i = 0; i < 30; ++i) a.step(0.7f, 1.0f / 30.0f);
    for (int i = 0; i < 60; ++i) b.step(0.7f, 1.0f / 60.0f);
    CHECK(a.position == doctest::Approx(b.position).epsilon(0.01));
    CHECK(vuDeflection(0.0f) == doctest::Approx(0.708f).epsilon(0.01));
    CHECK(vuDeflection(3.0f) == doctest::Approx(1.0f));
    CHECK(vuFromDeflection(vuDeflection(-7.0f)) == doctest::Approx(-7.0f).epsilon(0.001));
}
