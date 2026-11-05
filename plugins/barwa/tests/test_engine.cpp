#include "doctest.h"
#include "BarwaEngine.h"
#include "Fft.h"
#include "Measure.h"
#include "TestSignals.h"
#include <atomic>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <random>

using namespace wers;
using namespace wers::test;
using namespace barwa;

static std::atomic<bool> gCounting{ false };
static std::atomic<long> gAllocations{ 0 };
void* operator new(std::size_t n) {
    if (gCounting.load(std::memory_order_relaxed)) gAllocations.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr double kFs = 48000.0;
constexpr std::array<Character, 4> kCharacters{ Character::Lamp, Character::Tape, Character::Transformer, Character::Transistor };
constexpr std::array<double, 6> kRates{ 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

// A plain starting point. The real default of the mask comes from the track name, so tests set it themselves.
BarwaParams plain(float driveDb = 0.0f, int bands = 1) {
    BarwaParams p;
    p.bands = bands;
    p.mask = 0.0f;
    p.autoLevel = false;
    for (auto& b : p.band) b.driveDb = driveDb;
    return p;
}

std::vector<float> runThrough(BarwaEngine& engine, const std::vector<float>& x, int block = 256) {
    std::vector<float> y = x;
    for (size_t from = 0; from < y.size(); from += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), y.size() - from));
        float* channels[] = { y.data() + from };
        engine.process(channels, 1, n);
    }
    return y;
}

std::vector<float> run(const std::vector<float>& x, const BarwaParams& p, double fs = kFs, int quality = 2, int block = 256) {
    BarwaEngine engine;
    engine.prepare(fs, quality);
    engine.setParams(p);
    return runThrough(engine, x, block);
}

double errorAgainstDelayDb(const std::vector<float>& x, const std::vector<float>& y, int latency, int from = 0) {
    double error = 0.0, signal = 0.0;
    for (size_t i = static_cast<size_t>(latency + from); i < y.size(); ++i) {
        const double d = static_cast<double>(y[i]) - x[i - static_cast<size_t>(latency)];
        error += d * d;
        signal += static_cast<double>(x[i - static_cast<size_t>(latency)]) * x[i - static_cast<size_t>(latency)];
    }
    return error <= 0.0 ? -400.0 : 10.0 * std::log10(error / signal);
}

// The worst distance from 0 dB in the band, from the impulse response of the whole engine.
double flatnessDb(const BarwaParams& p, double fs, int quality = 2) {
    constexpr int kSize = 65536;
    std::vector<float> impulse(kSize, 0.0f);
    impulse[0] = 0.5f;
    const auto y = run(impulse, p, fs, quality);
    std::vector<std::complex<double>> h(kSize);
    for (size_t i = 0; i < y.size(); ++i) h[i] = static_cast<double>(y[i]) * 2.0;
    Fft(kSize).transform(h);
    double worst = 0.0;
    for (int bin = 1; bin < kSize / 2; ++bin) {
        const double hz = bin * fs / kSize;
        if (hz >= 20.0 && hz <= 20000.0) worst = std::max(worst, std::abs(20.0 * std::log10(std::abs(h[static_cast<size_t>(bin)]) + 1e-20)));
    }
    return worst;
}

double worstJump(const std::vector<float>& y, int from, int to) {
    float worst = 0.0f;
    for (int i = std::max(1, from); i < to; ++i) worst = std::max(worst, std::abs(y[static_cast<size_t>(i)] - y[static_cast<size_t>(i - 1)]));
    return static_cast<double>(worst);
}

double weightedDb(const std::vector<float>& x, int from, int to, double fs = kFs) {
    KWeighting weight;
    weight.prepare(fs);
    double energy = 0.0;
    for (int i = 0; i < to; ++i) {
        const double v = weight.tick(static_cast<double>(x[static_cast<size_t>(i)]));
        if (i >= from) energy += v * v;
    }
    return 10.0 * std::log10(energy / (to - from) + 1e-30);
}

} // namespace

TEST_CASE("one band at zero drive is a pure delay") {
    const auto x = loudPhrase(kFs, 2.0, -3.0);
    for (bool autoLevel : { false, true }) {
        auto p = plain();
        p.autoLevel = autoLevel;
        BarwaEngine engine;
        engine.prepare(kFs, 2);
        engine.setParams(p);
        const auto y = runThrough(engine, x);
        CHECK(errorAgainstDelayDb(x, y, engine.latencySamples()) < -140.0);
    }
}

TEST_CASE("three bands at zero drive are flat") {
    CHECK(flatnessDb(plain(0.0f, 3), kFs) < 0.05);
    CHECK(flatnessDb(plain(0.0f, 2), kFs) < 0.05);
}

TEST_CASE("latency matches quality") {
    for (const auto& [quality, oversampling] : std::array<std::pair<int, int>, 3>{ { { 1, 49 }, { 2, 57 }, { 3, 59 } } }) {
        BarwaEngine engine;
        engine.prepare(kFs, quality);
        CHECK(engine.latencySamples() == 192 + oversampling);
    }
    BarwaEngine high;
    high.prepare(192000.0, 3);
    CHECK(high.latencySamples() == 768 + 49);
}

TEST_CASE("same result in any block size") {
    const auto x = loudPhrase(kFs, 8.0, -3.0);
    auto p = plain(12.0f, 3);
    p.mask = 1.0f;
    p.autoLevel = true;
    p.tone = 0.3f;
    const auto reference = run(x, p, kFs, 2, 32);
    for (int block : { 256, 4096 }) {
        CAPTURE(block);
        const auto y = run(x, p, kFs, 2, block);
        CHECK(std::memcmp(y.data(), reference.data(), y.size() * sizeof(float)) == 0);
    }
    BarwaEngine engine;
    engine.prepare(kFs, 2);
    engine.setParams(p);
    std::vector<float> y = x;
    std::mt19937 random(7);
    for (size_t from = 0; from < y.size();) {
        const int n = std::min(static_cast<int>(random() % 700) + 1, static_cast<int>(y.size() - from));
        float* channels[] = { y.data() + from };
        engine.process(channels, 1, n);
        from += static_cast<size_t>(n);
    }
    CHECK(std::memcmp(y.data(), reference.data(), y.size() * sizeof(float)) == 0);
}

TEST_CASE("works at every sample rate") {
    for (double fs : kRates) {
        CAPTURE(fs);
        const auto x = loudPhrase(fs, 1.5, -3.0);
        BarwaEngine engine;
        engine.prepare(fs, 2);
        engine.setParams(plain());
        const auto y = runThrough(engine, x);
        CHECK(errorAgainstDelayDb(x, y, engine.latencySamples()) < -140.0);
        CHECK(flatnessDb(plain(0.0f, 3), fs) < 0.05);
        auto driven = plain(12.0f, 3);
        driven.mask = 1.0f;
        driven.autoLevel = true;
        for (float v : run(x, driven, fs)) REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("auto level holds loudness") {
    const auto x = loudPhrase(kFs, 6.0, -1.0);
    const int from = static_cast<int>(kFs * 2.0), to = static_cast<int>(x.size());
    const double inLevel = rmsDb(x, from, to), inWeighted = weightedDb(x, from, to), inPeak = peakDb(x, from, to);
    for (Character character : kCharacters) {
        for (float drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f }) {
            CAPTURE(static_cast<int>(character)); CAPTURE(drive);
            auto p = plain(drive);
            p.autoLevel = true;
            p.band[0].character = character;
            BarwaEngine engine;
            engine.prepare(kFs, 2);
            engine.setParams(p);
            const auto y = runThrough(engine, x);
            const int late = engine.latencySamples();
            const double level = rmsDb(y, from + late, to) - inLevel;
            const double weighted = weightedDb(y, from + late, to) - inWeighted;
            const double peak = peakDb(y, from + late, to) - inPeak;
            MESSAGE("character " << static_cast<int>(character) << " drive " << drive << ": rms " << level << " dB, weighted " << weighted << " dB, peak " << peak << " dB");
            CHECK(std::abs(weighted) <= 1.0);
            CHECK(peak <= 1.0);
        }
    }
}

namespace {

// Vowels and sibilants in turn, with the place of each transition.
struct Syllables {
    std::vector<float> signal;
    std::vector<std::pair<int, int>> vowels, sibilants;
};

Syllables makeSyllables(double fs, int rounds) {
    Syllables s;
    const int vowelLength = static_cast<int>(fs * 0.4), sibilantLength = static_cast<int>(fs * 0.2);
    const auto v = vowel(150.0, -18.0, vowelLength, fs);
    const auto n = bandNoise(4000.0, 9000.0, -26.0, sibilantLength, fs);
    for (int i = 0; i < rounds; ++i) {
        const int start = static_cast<int>(s.signal.size());
        append(s.signal, v);
        s.vowels.emplace_back(start, start + vowelLength);
        append(s.signal, n);
        s.sibilants.emplace_back(start + vowelLength, start + vowelLength + sibilantLength);
    }
    return s;
}

double zoneEnergy(const std::vector<float>& y, const std::vector<std::pair<int, int>>& zones, int skip, int late) {
    double energy = 0.0;
    for (const auto& [from, to] : zones)
        for (int i = from + skip + late; i < std::min(to + late, static_cast<int>(y.size())); ++i) energy += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    return energy;
}

} // namespace

TEST_CASE("mask spares sibilants") {
    const auto s = makeSyllables(kFs, 8);
    auto p = plain(12.0f);
    p.delta = true;
    BarwaEngine probe;
    probe.prepare(kFs, 2);
    const int late = probe.latencySamples();
    const int skip = static_cast<int>(kFs * 0.03);
    for (Character character : kCharacters) {
        CAPTURE(static_cast<int>(character));
        p.band[0].character = character;
        p.mask = 0.0f;
        const auto open = run(s.signal, p);
        p.mask = 1.0f;
        const auto masked = run(s.signal, p);
        const double sibilantDrop = 10.0 * std::log10(zoneEnergy(open, s.sibilants, skip, late) / zoneEnergy(masked, s.sibilants, skip, late));
        const double vowelDrop = 10.0 * std::log10(zoneEnergy(open, s.vowels, skip, late) / zoneEnergy(masked, s.vowels, skip, late));
        MESSAGE("character " << static_cast<int>(character) << ": sibilants down " << sibilantDrop << " dB, vowels down " << vowelDrop << " dB");
        CHECK(sibilantDrop >= 12.0);
        CHECK(vowelDrop <= 1.0);
    }
}

TEST_CASE("mask zero ignores the classifier") {
    // The same formula built from the parts, with no classifier anywhere.
    const auto s = makeSyllables(kFs, 3);
    const auto& x = s.signal;
    auto p = plain(12.0f);
    BarwaEngine engine;
    engine.prepare(kFs, 2);
    engine.setParams(p);
    const auto y = runThrough(engine, x);
    const int look = engine.latencySamples() - 57;
    Oversampler os;
    os.prepare(2);
    CharacterShaper shaper;
    shaper.prepare(kFs * 4.0);
    DcBlock dc;
    dc.prepare(kFs);
    DelayLine dry;
    dry.prepare(57);
    const float gain = dbToGain(12.0f);
    std::vector<float> expected(x.size(), 0.0f);
    std::array<float, 8> frame{};
    for (size_t i = static_cast<size_t>(look); i < x.size(); ++i) {
        const float in = x[i - static_cast<size_t>(look)];
        os.up(in, frame.data());
        for (int k = 0; k < 4; ++k) frame[static_cast<size_t>(k)] = shaper.tick(frame[static_cast<size_t>(k)], gain);
        const float wet = os.down(frame.data());
        const float d = dry.process(in);
        expected[i] = d + static_cast<float>(dc.tick(static_cast<double>(wet - d)));
    }
    double worst = 0.0;
    for (size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::abs(static_cast<double>(y[i]) - expected[i]));
    CHECK(20.0 * std::log10(worst + 1e-20) < -100.0);
    CHECK(engine.maskNow() == 1.0f);
}

TEST_CASE("hot input stays finite") {
    // Tones that repeat every block, clipped at +6 dBFS, so every block at the input is the same and a
    // change between blocks can only come from the engine.
    constexpr int kBlock = 256;
    const int n = static_cast<int>(kFs * 3.0);
    std::vector<float> steady(static_cast<size_t>(n));
    const double oneBlock = kFs / kBlock;
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * kPi * oneBlock * i / kFs;
        steady[static_cast<size_t>(i)] = static_cast<float>(std::clamp(1.5 * std::sin(t) + 0.8 * std::sin(10.0 * t) + 0.6 * std::sin(30.0 * t), -2.0, 2.0));
    }
    const auto phrase = loudPhrase(kFs, 3.0, 6.0);
    for (Character character : kCharacters) {
        for (float mask : { 0.0f, 1.0f }) {
            CAPTURE(static_cast<int>(character)); CAPTURE(mask);
            auto p = plain(12.0f, 3);
            p.autoLevel = true;
            p.mask = mask;
            for (auto& b : p.band) b.character = character;
            BarwaEngine engine;
            engine.prepare(kFs, 2);
            engine.setParams(p);
            const auto y = runThrough(engine, steady, kBlock);
            for (float v : y) REQUIRE(std::isfinite(v));
            double previous = 0.0, worst = 0.0;
            for (int b = static_cast<int>(kFs) / kBlock; b < n / kBlock; ++b) {
                const double level = rmsDb(y, b * kBlock, (b + 1) * kBlock);
                if (b > static_cast<int>(kFs) / kBlock) worst = std::max(worst, std::abs(level - previous));
                previous = level;
            }
            MESSAGE("character " << static_cast<int>(character) << " mask " << mask << ": largest change between blocks " << worst << " dB");
            CHECK(worst <= 1.0);
            for (float v : run(phrase, p)) REQUIRE(std::isfinite(v));
        }
    }
}

TEST_CASE("mask on a beat does not click or pump") {
    const int beatEvery = static_cast<int>(kFs * 0.25), hit = 480;
    for (double level : { -20.0, -12.0 }) {
        const int n = static_cast<int>(kFs * 6.0);
        auto beat = bandNoise(200.0, 8000.0, level, n, kFs);
        for (int i = 0; i < n; ++i) {
            const int since = i % beatEvery;
            if (since < hit) beat[static_cast<size_t>(i)] += 0.5f * std::exp(-static_cast<float>(since) / 60.0f) * (since % 2 == 0 ? 1.0f : -1.0f);
        }
        auto p = plain(12.0f);
        p.autoLevel = true;
        const auto open = run(beat, p);
        p.mask = 1.0f;
        BarwaEngine engine;
        engine.prepare(kFs, 2);
        engine.setParams(p);
        const auto masked = runThrough(engine, beat);
        const int late = engine.latencySamples();
        const int from = static_cast<int>(kFs * 2.0);

        // A hit that gets less colour keeps the edge it has at the input. That is not a click.
        const double hitLimit = 2.0 * std::max(worstJump(open, from, n), worstJump(beat, from, n));
        const double hitJump = worstJump(masked, from, n);
        // Between the hits there is only noise, where anything the mask does would stand out.
        double openJump = 0.0, maskedJump = 0.0;
        const int gap = static_cast<int>(kFs * 0.02);
        for (int start = from - from % beatEvery + late + hit + gap; start + beatEvery - hit - gap < n; start += beatEvery) {
            openJump = std::max(openJump, worstJump(open, start, start + beatEvery - hit - gap));
            maskedJump = std::max(maskedJump, worstJump(masked, start, start + beatEvery - hit - gap));
        }
        double worstLevel = 0.0;
        const int window = static_cast<int>(kFs * 0.1);
        for (int w = from; w + window < n; w += window) worstLevel = std::max(worstLevel, std::abs(rmsDb(masked, w, w + window) - rmsDb(open, w, w + window)));
        MESSAGE("beat at " << level << " dB: on hits " << hitJump << " of " << hitLimit << ", between hits " << maskedJump << " against " << openJump << ", level moves " << worstLevel << " dB");
        CHECK(hitJump <= hitLimit);
        CHECK(maskedJump <= 2.0 * openJump);
        CHECK(worstLevel <= 1.0);
    }
}

TEST_CASE("controls do not click") {
    const int half = static_cast<int>(kFs * 0.5);
    const auto x = sine(220.0, -12.0, half * 2, kFs);
    const auto constant = [&](const BarwaParams& p) {
        const auto y = run(x, p);
        return worstJump(y, half / 2, half * 2);
    };
    struct Change { const char* name; BarwaParams from, to; };
    std::vector<Change> changes;
    const auto add = [&](const char* name, BarwaParams from, BarwaParams to) { changes.push_back({ name, from, to }); };
    const auto base = plain(12.0f);
    auto q = base;
    q.band[0].driveDb = 0.0f; add("drive up", q, [&] { auto r = q; r.band[0].driveDb = 24.0f; return r; }());
    q = base; q.band[0].driveDb = 24.0f; add("drive down", q, [&] { auto r = q; r.band[0].driveDb = 0.0f; return r; }());
    q = base; q.mix = 0.0f; add("mix", q, base);
    q = base; q.band[0].mix = 0.0f; add("band mix", q, base);
    q = base; q.tone = -1.0f; add("tone", q, [&] { auto r = q; r.tone = 1.0f; return r; }());
    q = base; add("mask", q, [&] { auto r = q; r.mask = 1.0f; return r; }());
    q = base; add("output up", q, [&] { auto r = q; r.outputDb = 12.0f; return r; }());
    q = base; add("output down", q, [&] { auto r = q; r.outputDb = -12.0f; return r; }());
    q = base; add("character", q, [&] { auto r = q; r.band[0].character = Character::Transistor; return r; }());
    q = base; add("character with shelves", q, [&] { auto r = q; r.band[0].character = Character::Transformer; return r; }());
    q = plain(12.0f, 1); add("one to three bands", q, plain(12.0f, 3));
    q = plain(12.0f, 3); add("three to one band", q, plain(12.0f, 1));
    q = plain(12.0f, 3); add("two splits move", q, [&] { auto r = q; r.f1 = 1000.0f; r.f2 = 10000.0f; return r; }());
    q = base; add("auto level", q, [&] { auto r = q; r.autoLevel = true; return r; }());
    q = base; add("delta", q, [&] { auto r = q; r.delta = true; return r; }());
    q = plain(12.0f, 3); add("solo", q, [&] { auto r = q; r.band[0].solo = true; return r; }());
    for (const auto& change : changes) {
        CAPTURE(change.name);
        BarwaEngine engine;
        engine.prepare(kFs, 2);
        engine.setParams(change.from);
        std::vector<float> y = x;
        for (int from = 0; from < half * 2; from += 256) {
            if (from >= half && from < half + 256) engine.setParams(change.to);
            const int n = std::min(256, half * 2 - from);
            float* channels[] = { y.data() + from };
            engine.process(channels, 1, n);
        }
        const double allowed = 2.0 * std::max(constant(change.from), constant(change.to));
        const double jump = worstJump(y, half - 256, half * 2);
        MESSAGE(doctest::String(change.name) << ": jump " << jump << ", allowed " << allowed);
        CHECK(jump <= allowed);
    }
}

TEST_CASE("delta plus dry equals output") {
    const auto x = loudPhrase(kFs, 2.0, -3.0);
    auto p = plain(18.0f);
    p.tone = 0.5f;
    const auto output = run(x, p);
    p.delta = true;
    const auto delta = run(x, p);
    BarwaEngine probe;
    probe.prepare(kFs, 2);
    const int late = probe.latencySamples();
    double worst = 0.0;
    for (size_t i = static_cast<size_t>(late); i < x.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(delta[i]) + x[i - static_cast<size_t>(late)] - output[i]));
    CHECK(20.0 * std::log10(worst + 1e-20) < -100.0);
}

TEST_CASE("solo keeps only one band") {
    const auto x = noise(-18.0, static_cast<int>(kFs));
    const auto full = run(x, plain(0.0f, 3));
    std::vector<float> sum(x.size(), 0.0f);
    for (int band = 0; band < 3; ++band) {
        auto p = plain(0.0f, 3);
        p.band[static_cast<size_t>(band)].solo = true;
        const auto y = run(x, p);
        for (size_t i = 0; i < y.size(); ++i) sum[i] += y[i];
    }
    CHECK(errorAgainstDelayDb(full, sum, 0, 2000) < -60.0);
    // A soloed band with the others heard is not the same as the whole.
    auto low = plain(0.0f, 3);
    low.band[0].solo = true;
    const auto lowOnly = run(x, low);
    CHECK(rmsDb(lowOnly, 2000) < rmsDb(full, 2000) - 6.0);
}

TEST_CASE("stereo shares one mask and keeps its channels apart") {
    const auto phrase = loudPhrase(kFs, 2.0, -6.0);
    auto p = plain(12.0f, 3);
    p.mask = 1.0f;
    p.autoLevel = true;
    const auto mono = run(phrase, p);
    BarwaEngine engine;
    engine.prepare(kFs, 2);
    engine.setParams(p);
    auto left = phrase, right = phrase;
    for (size_t from = 0; from < left.size(); from += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, left.size() - from));
        float* channels[] = { left.data() + from, right.data() + from };
        engine.process(channels, 2, n);
    }
    // The same sound in both channels is the same as mono.
    CHECK(std::memcmp(left.data(), mono.data(), left.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(right.data(), mono.data(), right.size() * sizeof(float)) == 0);

    // What is silent in one channel stays silent, whatever the other one does.
    BarwaEngine oneSided;
    oneSided.prepare(kFs, 2);
    oneSided.setParams(p);
    auto loud = phrase;
    std::vector<float> quiet(phrase.size(), 0.0f);
    for (size_t from = 0; from < loud.size(); from += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, loud.size() - from));
        float* channels[] = { loud.data() + from, quiet.data() + from };
        oneSided.process(channels, 2, n);
    }
    for (float v : quiet) REQUIRE(v == 0.0f);
    CHECK(rmsDb(loud) > -40.0);
}

TEST_CASE("fuzz never poisons") {
    std::mt19937 random(11);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const std::array<float, 8> wild{ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                     1.0e30f, -1.0e30f, 1.0f, 1.0e-40f, 0.0f };
    for (double fs : { 44100.0, 192000.0 }) {
        CAPTURE(fs);
        BarwaEngine engine;
        engine.prepare(fs, 3);
        std::array<std::vector<float>, 2> buffer;
        for (auto& b : buffer) b.resize(512);
        float* channels[] = { buffer[0].data(), buffer[1].data() };
        for (int block = 0; block < 400; ++block) {
            BarwaParams p;
            p.bands = 1 + static_cast<int>(random() % 3);
            p.f1 = 60.0f + 940.0f * unit(random);
            p.f2 = 1000.0f + 9000.0f * unit(random);
            for (auto& b : p.band) {
                b.character = kCharacters[random() % 4];
                b.driveDb = 36.0f * unit(random);
                b.mix = unit(random);
                b.solo = random() % 4 == 0;
            }
            p.mask = unit(random);
            p.tone = 2.0f * unit(random) - 1.0f;
            p.mix = unit(random);
            p.outputDb = 48.0f * unit(random) - 24.0f;
            p.autoLevel = random() % 2 == 0;
            p.delta = random() % 4 == 0;
            engine.setParams(p);
            for (auto& b : buffer) {
                const int kind = static_cast<int>(random() % 4);
                for (auto& v : b) v = kind == 0 ? wild[random() % wild.size()] : (kind == 1 ? 1.0f : 2.0f * unit(random) - 1.0f);
            }
            engine.process(channels, 2, 512);
            for (const auto& b : buffer) for (float v : b) REQUIRE(std::isfinite(v));
        }
        // A second of ordinary signal with neutral settings brings everything back.
        auto p = plain();
        p.autoLevel = true;
        engine.setParams(p);
        const auto x = sine(1000.0, -12.0, static_cast<int>(fs), fs);
        std::vector<float> left = x, right = x;
        for (size_t from = 0; from < x.size(); from += 512) {
            const int n = static_cast<int>(std::min<size_t>(512, x.size() - from));
            float* now[] = { left.data() + from, right.data() + from };
            engine.process(now, 2, n);
        }
        const int tail = static_cast<int>(fs * 0.1);
        CHECK(std::abs(rmsDb(left, static_cast<int>(x.size()) - tail) - rmsDb(x, static_cast<int>(x.size()) - tail)) < 1.0);
    }
}

TEST_CASE("audio thread never allocates") {
    BarwaEngine engine;
    engine.prepare(kFs, 3);
    auto left = loudPhrase(kFs, 1.0, -3.0);
    auto right = left;
    auto p = plain(12.0f, 3);
    p.mask = 1.0f;
    p.autoLevel = true;
    gAllocations = 0;
    gCounting = true;
    for (size_t from = 0; from + 256 <= left.size(); from += 256) {
        p.band[0].character = kCharacters[(from / 256 / 40) % 4];
        p.bands = 1 + static_cast<int>((from / 256 / 60) % 3);
        p.tone = from % 512 == 0 ? 0.5f : -0.5f;
        engine.setParams(p);
        float* channels[] = { left.data() + from, right.data() + from };
        engine.process(channels, 2, 256);
        REQUIRE(engine.latencySamples() > 0);
    }
    gCounting = false;
    CHECK(gAllocations.load() == 0);
}
