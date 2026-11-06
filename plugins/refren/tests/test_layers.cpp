#include "doctest.h"
#include "AllocationCount.h"
#include "Measure.h"
#include "RefrenEngine.h"
#include "TestSignals.h"
#include <random>

using namespace refren;
using namespace wers::test;

namespace {

constexpr double kFs = 48000.0;

int latencyOf(double fs) { return static_cast<int>(std::lround(0.030 * fs)); }

// One layer of one voice with no drift and no mask, which is as simple as a layer gets.
RefrenParams simple(Source source = Source::Voice, int semis = 0) {
    RefrenParams p;
    p.consonants = 0.0f;
    auto& l = p.layer[0];
    l.on = true;
    l.source = source;
    l.semis = semis;
    l.voices = 1;
    l.timeMs = 0.0f;
    l.detuneCents = 0.0f;
    l.width = 0.0f;
    l.levelDb = 0.0f;
    return p;
}

struct Stereo {
    std::vector<float> left, right;
};

Stereo runStereo(const std::vector<float>& inLeft, const std::vector<float>& inRight, const RefrenParams& p, double fs = kFs, int block = 256) {
    RefrenEngine engine;
    engine.prepare(fs);
    engine.setParams(p);
    Stereo y{ inLeft, inRight };
    for (size_t from = 0; from < y.left.size(); from += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), y.left.size() - from));
        float* channels[] = { y.left.data() + from, y.right.data() + from };
        engine.process(channels, 2, n);
    }
    return y;
}

Stereo runMono(const std::vector<float>& x, const RefrenParams& p, double fs = kFs) { return runStereo(x, x, p, fs); }

std::vector<float> sumOf(const Stereo& s) {
    std::vector<float> m(s.left.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.left[i] + s.right[i]);
    return m;
}

std::vector<float> sidesOf(const Stereo& s) {
    std::vector<float> m(s.left.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.left[i] - s.right[i]);
    return m;
}

template <typename Wanted>
double shareOnPitch(const std::vector<float>& y, double fs, double cents, Wanted&& wantedHz) {
    const int late = latencyOf(fs), step = static_cast<int>(fs * 0.025);
    int total = 0, close = 0;
    for (int start = static_cast<int>(fs * 0.2);; start += step) {
        const double first = wantedHz(static_cast<double>(start - late) / fs);
        const int length = static_cast<int>(3.0 * fs / first);
        if (start + length + static_cast<int>(1.2 * fs / first) + 2 >= static_cast<int>(y.size())) break;
        const double wanted = wantedHz((start + 0.5 * length - late) / fs);
        const double hz = framePitchHz(y, start, length, fs, wanted / 1.15, wanted * 1.15);
        ++total;
        if (hz > 0.0 && std::abs(centsBetween(hz, wanted)) <= cents) ++close;
    }
    REQUIRE(total > 5);
    return static_cast<double>(close) / total;
}

} // namespace

TEST_CASE("amount zero is a pure delay") {
    for (double fs : kRates) {
        CAPTURE(fs);
        const int n = static_cast<int>(fs * 0.5);
        const auto left = loudPhrase(fs, 0.5, -3.0, 5u), right = loudPhrase(fs, 0.5, -3.0, 9u);
        RefrenParams p;
        p.amount = 0.0f;
        for (auto& l : p.layer) l.on = true;
        p.layer[1].source = Source::Whisper;
        p.layer[2].source = Source::Sub;
        p.layer[3].source = Source::Robot;
        const auto y = runStereo(left, right, p, fs);
        REQUIRE(static_cast<int>(y.left.size()) == n);
        CHECK(errorAgainstDelayDb(left, y.left, latencyOf(fs)) < -140.0);
        CHECK(errorAgainstDelayDb(right, y.right, latencyOf(fs)) < -140.0);
    }
}

TEST_CASE("latency is thirty milliseconds") {
    for (double fs : kRates) {
        CAPTURE(fs);
        RefrenEngine engine;
        engine.prepare(fs);
        CHECK(engine.latencySamples() == latencyOf(fs));
        const int n = latencyOf(fs) * 3;
        std::vector<float> impulse(static_cast<size_t>(n), 0.0f);
        impulse[100] = 0.5f;
        // The dry path, and a voice with no offset through the whole wet path.
        for (bool wet : { false, true }) {
            CAPTURE(wet);
            auto p = simple();
            p.delta = wet;
            p.amount = wet ? 1.0f : 0.0f;
            const auto y = sumOf(runMono(impulse, p, fs));
            int peak = 0;
            for (int i = 0; i < n; ++i)
                if (std::abs(y[static_cast<size_t>(i)]) > std::abs(y[static_cast<size_t>(peak)])) peak = i;
            // The high cut of a layer at 20 kHz delays by some 10 microseconds, which is a few samples at a high rate.
            CHECK(std::abs(peak - (100 + latencyOf(fs))) <= (wet ? static_cast<int>(std::ceil(fs * 12.0e-6)) : 0));
        }
    }
}

TEST_CASE("same result in any block size") {
    RefrenParams p;
    for (auto& l : p.layer) l.on = true;
    p.layer[0].semis = 0;
    p.layer[1].semis = 12;
    p.layer[1].voices = 3;
    p.layer[2].source = Source::Whisper;
    p.layer[3].source = Source::Sub;
    p.layer[3].semis = -12;
    CHECK(sameInAnyBlockSize({ loudPhrase(kFs, 4.0, -3.0, 5u), loudPhrase(kFs, 4.0, -3.0, 9u) }, [&] {
        auto engine = std::make_shared<RefrenEngine>();
        engine->prepare(kFs);
        engine->setParams(p);
        return [engine](float* const* channels, int count, int n) { engine->process(channels, count, n); };
    }));
    p.layer[0].source = Source::Robot;
    p.layer[1].voices = 16;
    CHECK(sameInAnyBlockSize({ loudPhrase(kFs, 2.0, -3.0, 5u) }, [&] {
        auto engine = std::make_shared<RefrenEngine>();
        engine->prepare(kFs);
        engine->setParams(p);
        return [engine](float* const* channels, int count, int n) { engine->process(channels, count, n); };
    }));
}

TEST_CASE("every sample rate works") {
    for (double fs : kRates) {
        CAPTURE(fs);
        auto p = simple(Source::Voice, -12);
        p.delta = true;
        const auto y = sumOf(runMono(vowel(150.0, -18.0, static_cast<int>(fs), fs), p, fs));
        for (float v : y) REQUIRE(std::isfinite(v));
        CHECK(shareOnPitch(y, fs, 5.0, [](double) { return 75.0; }) >= 0.9);
    }
}

TEST_CASE("voices share the limit of 32") {
    const auto activeWith = [](int a, int b, int c, int d) {
        RefrenParams p;
        const std::array<int, 4> counts{ a, b, c, d };
        for (size_t k = 0; k < 4; ++k) {
            p.layer[k].on = true;
            p.layer[k].voices = counts[k];
        }
        RefrenEngine engine;
        engine.prepare(kFs);
        engine.setParams(p);
        std::vector<float> x(256, 0.0f);
        float* channels[] = { x.data() };
        engine.process(channels, 1, 256);
        return std::array<int, 4>{ engine.activeVoices(0), engine.activeVoices(1), engine.activeVoices(2), engine.activeVoices(3) };
    };
    CHECK(activeWith(16, 16, 16, 16) == std::array<int, 4>{ 16, 16, 0, 0 });
    CHECK(activeWith(12, 12, 12, 12) == std::array<int, 4>{ 12, 12, 8, 0 });
    CHECK(activeWith(2, 2, 2, 2) == std::array<int, 4>{ 2, 2, 2, 2 });
}

TEST_CASE("more voices are not louder") {
    const auto x = loudPhrase(kFs, 4.0, -6.0);
    const auto levelWith = [&](int voices) {
        auto p = simple();
        p.delta = true;
        p.layer[0].voices = voices;
        p.layer[0].timeMs = 30.0f;
        p.layer[0].width = 1.0f;
        p.layer[0].detuneCents = 10.0f;
        const auto y = runMono(x, p);
        return 0.5 * (rmsDb(y.left, 4800) + rmsDb(y.right, 4800));
    };
    const double two = levelWith(2), sixteen = levelWith(16);
    MESSAGE("2 voices " << two << " dB, 16 voices " << sixteen << " dB, input " << rmsDb(x, 4800) << " dB");
    CHECK(std::abs(two - sixteen) <= 1.5);
}

TEST_CASE("width spreads with equal power") {
    const auto x = loudPhrase(kFs, 3.0, -6.0);
    const auto powerAt = [&](float width, Stereo& y) {
        auto p = simple();
        p.delta = true;
        p.layer[0].voices = 4;
        p.layer[0].timeMs = 20.0f;
        p.layer[0].width = width;
        y = runMono(x, p);
        return 10.0 * std::log10(std::pow(10.0, rmsDb(y.left, 4800) / 10.0) + std::pow(10.0, rmsDb(y.right, 4800) / 10.0));
    };
    Stereo narrow, wide;
    const double narrowPower = powerAt(0.0f, narrow), widePower = powerAt(1.0f, wide);
    for (size_t i = 0; i < narrow.left.size(); ++i) REQUIRE(narrow.left[i] == narrow.right[i]);
    CHECK(rmsDb(sidesOf(wide), 4800) > rmsDb(sumOf(wide), 4800) - 12.0);
    CHECK(std::abs(narrowPower - widePower) <= 1.5);
}

TEST_CASE("multipliers stop at the layer range") {
    const auto x = loudPhrase(kFs, 1.0, -6.0);
    auto p = simple();
    p.delta = true;
    p.layer[0].voices = 4;
    p.layer[0].timeMs = 40.0f;
    p.layer[0].width = 1.0f;
    const auto full = runMono(x, p);
    p.timeScale = 2.0f;
    p.widthScale = 2.0f;
    const auto doubled = runMono(x, p);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE(full.left[i] == doubled.left[i]);
    p.layer[0].timeMs = 20.0f;
    p.layer[0].width = 0.5f;
    const auto halfDoubled = runMono(x, p);
    double difference = 0.0;
    for (size_t i = 0; i < x.size(); ++i) difference = std::max(difference, static_cast<double>(std::abs(full.left[i] - halfDoubled.left[i])));
    CHECK(difference < 1.0e-3);
}

TEST_CASE("time offsets keep their range") {
    const int n = static_cast<int>(kFs * 0.2);
    const auto reach = [&](int semis, int& first, int& last) {
        LayerParams l;
        l.on = true;
        l.voices = 16;
        l.semis = semis;
        l.timeMs = 40.0f;
        l.detuneCents = 0.0f;
        l.width = 1.0f;
        l.levelDb = 0.0f;
        const int streamDelay = 0;
        Layer layer;
        layer.prepare(kFs, 3u);
        layer.set(l, 1.0f, 1.0f, 16, streamDelay);
        first = -1;
        last = -1;
        for (int i = 0; i < n; ++i) {
            float left = 0.0f, right = 0.0f;
            layer.tick(i == 0 ? 1.0f : 0.0f, left, right);
            if (std::abs(left) + std::abs(right) > 1.0e-4f) {
                if (first < 0) first = i;
                last = i;
            }
        }
    };
    const int top = latencyOf(kFs) - 49;
    int first = 0, last = 0;
    reach(0, first, last);
    CHECK(std::abs((first - top) / kFs * 1000.0 + 8.0) <= 1.0);
    CHECK(std::abs((last - top) / kFs * 1000.0 - 40.0) <= 1.0);
    reach(12, first, last);
    CHECK(std::abs((first - top) / kFs * 1000.0) <= 1.0);
    CHECK(std::abs((last - top) / kFs * 1000.0 - 40.0) <= 1.0);
}

TEST_CASE("detune is a slow drift") {
    auto p = simple();
    p.delta = true;
    p.layer[0].detuneCents = 50.0f;
    const double f0 = 400.0;
    const auto y = sumOf(runMono(sine(f0, -12.0, static_cast<int>(kFs * 8.0), kFs), p));
    double lowest = 1.0e9, highest = -1.0e9;
    for (int start = static_cast<int>(kFs * 0.5); start + 4000 < static_cast<int>(y.size()); start += 2400) {
        const double hz = framePitchHz(y, start, 2400, kFs, 300.0, 520.0);
        REQUIRE(hz > 0.0);
        const double cents = centsBetween(hz, f0);
        lowest = std::min(lowest, cents);
        highest = std::max(highest, cents);
    }
    MESSAGE("drift from " << lowest << " to " << highest << " cents");
    CHECK(lowest >= -55.0);
    CHECK(highest <= 55.0);
    CHECK(highest - lowest >= 10.0);
}

TEST_CASE("same seed after reset") {
    RefrenParams p;
    p.layer[0].voices = 6;
    p.layer[0].detuneCents = 20.0f;
    p.layer[1].on = true;
    p.layer[1].semis = 12;
    const auto x = loudPhrase(kFs, 1.0, -6.0);
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setParams(p);
    auto a = x;
    float* first[] = { a.data() };
    engine.process(first, 1, static_cast<int>(a.size()));
    engine.reset();
    auto b = x;
    float* second[] = { b.data() };
    engine.process(second, 1, static_cast<int>(b.size()));
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

TEST_CASE("layers line up with the dry voice") {
    const auto s = makeSyllables(kFs, 8);
    const int late = latencyOf(kFs);
    std::vector<float> dry(s.signal.size(), 0.0f);
    std::copy(s.signal.begin(), s.signal.end() - late, dry.begin() + late);
    for (int semis : { 0, 12, -12, 7 }) {
        CAPTURE(semis);
        auto p = simple(Source::Voice, semis);
        p.delta = true;
        const double lag = envelopeLagMs(dry, sumOf(runMono(s.signal, p)), kFs);
        MESSAGE("interval " << semis << ": " << lag << " ms");
        CHECK(std::abs(lag) <= 2.0);
    }
}

TEST_CASE("mask closes on sibilants") {
    const auto s = makeSyllables(kFs, 8);
    auto p = simple();
    p.delta = true;
    p.layer[0].voices = 8;
    p.layer[0].timeMs = 30.0f;
    p.layer[0].width = 1.0f;
    const auto late = latencyOf(kFs), skip = static_cast<int>(kFs * 0.035);
    p.consonants = 0.0f;
    const auto open = sidesOf(runMono(s.signal, p));
    p.consonants = 1.0f;
    const auto masked = sidesOf(runMono(s.signal, p));
    const double drop = 10.0 * std::log10(zoneEnergy(open, s.sibilants, skip, late) / (zoneEnergy(masked, s.sibilants, skip, late) + 1.0e-30));
    MESSAGE("sibilants on the sides down " << drop << " dB");
    CHECK(drop >= 12.0);
}

TEST_CASE("mask leaves vowels") {
    const auto s = makeSyllables(kFs, 8);
    for (int semis : { 0, 12 }) {
        CAPTURE(semis);
        auto p = simple(Source::Voice, semis);
        p.delta = true;
        p.layer[0].voices = 8;
        p.layer[0].timeMs = 30.0f;
        p.layer[0].width = 1.0f;
        const int late = latencyOf(kFs), skip = static_cast<int>(kFs * 0.06);
        p.consonants = 0.0f;
        const auto open = sumOf(runMono(s.signal, p));
        p.consonants = 1.0f;
        const auto masked = sumOf(runMono(s.signal, p));
        const double drop = 10.0 * std::log10(zoneEnergy(open, s.vowels, skip, late) / zoneEnergy(masked, s.vowels, skip, late));
        MESSAGE("interval " << semis << ": vowels down " << drop << " dB");
        CHECK(std::abs(drop) <= 1.0);
    }
}

TEST_CASE("mask off equals no classifier") {
    auto p = simple();
    p.delta = true;
    p.layer[0].voices = 4;
    p.layer[0].timeMs = 20.0f;
    p.layer[0].width = 0.0f;
    // With the mask off the path is linear, so its result is the input through its impulse response.
    const int n = static_cast<int>(kFs);
    const auto x = makeSyllables(kFs, 2).signal;
    std::vector<float> impulse(x.size(), 0.0f);
    impulse[0] = 1.0f;
    const auto h = runMono(impulse, p).left;
    const auto y = runMono(x, p).left;
    std::vector<double> expected(x.size(), 0.0);
    int nonZero = 0;
    for (size_t k = 0; k < h.size() && k < static_cast<size_t>(n); ++k) {
        if (std::abs(h[k]) < 1.0e-7f) continue;
        ++nonZero;
        for (size_t i = 0; i + k < x.size(); ++i) expected[i + k] += static_cast<double>(h[k]) * x[i];
    }
    REQUIRE(nonZero > 0);
    double error = 0.0, signal = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double d = static_cast<double>(y[i]) - expected[i];
        error += d * d;
        signal += static_cast<double>(y[i]) * y[i];
    }
    CHECK(10.0 * std::log10(error / signal) < -80.0);
}

TEST_CASE("mask does not touch whisper") {
    const auto s = makeSyllables(kFs, 3);
    auto p = simple(Source::Whisper);
    p.delta = true;
    p.consonants = 0.0f;
    const auto a = runMono(s.signal, p);
    p.consonants = 1.0f;
    const auto b = runMono(s.signal, p);
    for (size_t i = 0; i < s.signal.size(); ++i) REQUIRE(a.left[i] == b.left[i]);
    CHECK(rmsDb(a.left, 4800) > -60.0);
}

TEST_CASE("dry stays stereo") {
    const auto left = loudPhrase(kFs, 1.0, -6.0, 5u), right = loudPhrase(kFs, 1.0, -6.0, 9u);
    RefrenParams p;
    for (auto& l : p.layer) l.on = false;
    const auto y = runStereo(left, right, p);
    CHECK(errorAgainstDelayDb(left, y.left, latencyOf(kFs)) < -140.0);
    CHECK(errorAgainstDelayDb(right, y.right, latencyOf(kFs)) < -140.0);
}

TEST_CASE("mono output averages the wet") {
    const auto x = loudPhrase(kFs, 1.0, -6.0);
    RefrenParams p;
    p.layer[0].voices = 4;
    p.layer[0].width = 1.0f;
    p.layer[0].timeMs = 20.0f;
    const auto stereo = runMono(x, p);
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setParams(p);
    auto mono = x;
    float* channels[] = { mono.data() };
    engine.process(channels, 1, static_cast<int>(mono.size()));
    double worst = 0.0;
    const auto average = sumOf(stereo);
    for (size_t i = 0; i < x.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(mono[i] - average[i])));
    CHECK(20.0 * std::log10(worst + 1.0e-20) < -100.0);
}

TEST_CASE("one channel and opposite channels") {
    const auto x = loudPhrase(kFs, 2.0, -6.0);
    std::vector<float> silent(x.size(), 0.0f), opposite(x.size());
    for (size_t i = 0; i < x.size(); ++i) opposite[i] = -x[i];
    auto p = simple();
    p.layer[0].voices = 2;
    SUBCASE("the voice in one channel") {
        auto delta = p;
        delta.delta = true;
        const auto wetBoth = runStereo(x, x, delta);
        const auto wetOne = runStereo(x, silent, delta);
        const double difference = rmsDb(wetBoth.left, 4800) - rmsDb(wetOne.left, 4800);
        MESSAGE("copies quieter by " << difference << " dB");
        CHECK(difference <= 7.0);
        auto dryOnly = p;
        dryOnly.amount = 0.0f;
        CHECK(errorAgainstDelayDb(x, runStereo(x, silent, dryOnly).left, latencyOf(kFs)) < -140.0);
    }
    SUBCASE("opposite channels") {
        const auto y = runStereo(x, opposite, p);
        for (size_t i = 0; i < x.size(); ++i) {
            REQUIRE(std::isfinite(y.left[i]));
            REQUIRE(std::isfinite(y.right[i]));
        }
        CHECK(errorAgainstDelayDb(x, y.left, latencyOf(kFs)) < -140.0);
        CHECK(errorAgainstDelayDb(opposite, y.right, latencyOf(kFs)) < -140.0);
    }
}

TEST_CASE("gate keeps its timing") {
    auto p = simple();
    p.delta = true;
    p.layer[0].voices = 1;
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setParams(p);
    auto x = sine(200.0, -12.0, static_cast<int>(kFs * 1.5), kFs);
    const int change = static_cast<int>(kFs * 0.5);
    float* now[] = { x.data() };
    engine.process(now, 1, change);
    p.layer[0].formant = 2.0f;
    engine.setParams(p);
    float* rest[] = { x.data() + change };
    engine.process(rest, 1, static_cast<int>(x.size()) - change);
    // Where the level falls away and where it is back, in 1 ms steps.
    const int ms = static_cast<int>(kFs * 0.001);
    const auto levelAt = [&](int i) { return rmsDb(x, i, i + 5 * ms); };
    const double full = rmsDb(x, change - 100 * ms, change);
    const int end = static_cast<int>(x.size()) - 6 * ms;
    int quietFrom = change;
    while (quietFrom < end && levelAt(quietFrom) > full - 40.0) quietFrom += ms;
    REQUIRE(quietFrom < end);
    int quietTo = quietFrom;
    while (quietTo < end && levelAt(quietTo) < full - 40.0) quietTo += ms;
    const double fall = (quietFrom + 5 * ms - change) / kFs * 1000.0;
    MESSAGE("quiet after " << fall << " ms for " << (quietTo - quietFrom) / kFs * 1000.0 << " ms");
    CHECK(fall <= 27.0);
    CHECK(fall >= 10.0);
}

TEST_CASE("changes that cannot be smoothed do not click") {
    const int n = static_cast<int>(kFs * 2.0), change = static_cast<int>(kFs);
    const auto run = [&](const std::vector<float>& x, const RefrenParams& before, const RefrenParams& after) {
        RefrenEngine engine;
        engine.prepare(kFs);
        engine.setParams(before);
        auto y = x;
        for (int from = 0; from < n; from += 256) {
            if (from >= change && from - 256 < change) engine.setParams(after);
            float* channels[] = { y.data() + from };
            engine.process(channels, 1, std::min(256, n - from));
        }
        return y;
    };
    const auto base = [] {
        RefrenParams p;
        p.layer[0].voices = 4;
        p.layer[0].timeMs = 20.0f;
        return p;
    }();
    std::vector<std::pair<const char*, RefrenParams>> changes;
    { auto p = base; p.layer[0].source = Source::Whisper; changes.emplace_back("source", p); }
    { auto p = base; p.layer[0].voices = 12; changes.emplace_back("voices", p); }
    { auto p = base; p.layer[0].semis = 12; changes.emplace_back("interval", p); }
    { auto p = base; p.layer[0].formant = 4.0f; changes.emplace_back("colour", p); }
    { auto p = base; p.layer[0].source = Source::Robot; changes.emplace_back("robot", p); }
    { auto p = base; p.layer[1].on = true; changes.emplace_back("layer on", p); }
    for (const auto& x : { sine(150.0, -12.0, n, kFs), loudPhrase(kFs, 2.0, -6.0) }) {
        const auto steady = run(x, base, base);
        const double wanted = worstJump(steady, change, n);
        for (const auto& item : changes) {
            const char* name = item.first;
            const RefrenParams& after = item.second;
            const std::string what = name;
            CAPTURE(what);
            const auto y = run(x, base, after);
            for (float v : y) REQUIRE(std::isfinite(v));
            // The new setting may have larger steps of its own, as a whisper does on a tone.
            const double afterwards = worstJump(run(x, after, after), change, n);
            CHECK(worstJump(y, change, n) <= 2.0 * std::max(wanted, afterwards));
            // The dry voice goes on untouched under all of it.
            auto deltaBefore = base, deltaAfter = after;
            deltaBefore.delta = deltaAfter.delta = true;
            const auto wet = run(x, deltaBefore, deltaAfter);
            double worst = 0.0;
            for (int i = latencyOf(kFs); i < n; ++i)
                worst = std::max(worst, std::abs(static_cast<double>(y[static_cast<size_t>(i)]) - wet[static_cast<size_t>(i)] - x[static_cast<size_t>(i - latencyOf(kFs))]));
            CHECK(20.0 * std::log10(worst + 1.0e-20) < -100.0);
        }
    }
}

TEST_CASE("hot input stays finite") {
    const auto x = loudPhrase(kFs, 2.0, 5.9);
    RefrenParams p;
    p.layer[0].voices = 8;
    p.layer[1].on = true;
    p.layer[1].semis = 12;
    p.layer[2].on = true;
    p.layer[2].source = Source::Whisper;
    const auto y = runMono(x, p);
    for (float v : y.left) REQUIRE(std::isfinite(v));
    const double input = worstJump(x, 0, static_cast<int>(x.size())), output = worstJump(y.left, 0, static_cast<int>(x.size()));
    MESSAGE("worst step: input " << input << ", output " << output);
    CHECK(output <= 2.0 * input);
}

TEST_CASE("fuzz never poisons") {
    std::mt19937 random(11);
    const auto any = [&](float low, float high) { return low + (high - low) * static_cast<float>(random() % 10001) / 10000.0f; };
    RefrenEngine engine;
    engine.prepare(kFs);
    std::vector<float> x(2048);
    std::vector<float> y(2048);
    for (int round = 0; round < 200; ++round) {
        RefrenParams p;
        p.amount = any(-1.0f, 2.0f);
        p.dryDb = any(-100.0f, 20.0f);
        p.outputDb = round % 17 == 0 ? std::numeric_limits<float>::quiet_NaN() : any(-100.0f, 100.0f);
        p.widthScale = any(0.0f, 3.0f);
        p.timeScale = any(0.0f, 3.0f);
        p.consonants = round % 13 == 0 ? std::numeric_limits<float>::infinity() : any(0.0f, 1.0f);
        p.tilt = any(-2.0f, 2.0f);
        p.grainDb = round % 19 == 0 ? std::numeric_limits<float>::quiet_NaN() : any(-10.0f, 60.0f);
        p.grainChar = static_cast<Character>(random() % 4);
        p.motion = static_cast<Motion>(random() % 4);
        p.motionRate = any(-5.0f, 50.0f);
        p.motionDepth = any(-1.0f, 2.0f);
        p.echoMix = any(-1.0f, 2.0f);
        p.echoTime = static_cast<EchoTime>(random() % 7);
        p.echoFeedback = any(-1.0f, 3.0f);
        p.echoPing = random() % 2 == 0;
        p.echoToneHz = any(-100.0f, 90000.0f);
        p.verbMix = any(-1.0f, 2.0f);
        p.verbSize = any(-1.0f, 2.0f);
        p.verbDecay = round % 23 == 0 ? std::numeric_limits<float>::infinity() : any(-1.0f, 100.0f);
        p.verbToneHz = any(-100.0f, 90000.0f);
        p.verbPreMs = any(-10.0f, 500.0f);
        p.verbShimmer = any(-1.0f, 3.0f);
        p.duck = any(-1.0f, 2.0f);
        p.sendDry = any(-1.0f, 2.0f);
        p.trimDb = any(-100.0f, 100.0f);
        engine.setTempo(round % 11 == 0 ? std::numeric_limits<double>::quiet_NaN() : round % 7 == 0 ? 1.0e12 : static_cast<double>(any(-100.0f, 1000.0f)));
        for (auto& l : p.layer) {
            l.on = random() % 2 == 0;
            l.source = static_cast<Source>(random() % 4);
            l.voices = static_cast<int>(random() % 40) - 5;
            l.semis = static_cast<int>(random() % 100) - 50;
            l.formant = any(-30.0f, 30.0f);
            l.levelDb = any(-100.0f, 50.0f);
            l.width = any(-1.0f, 2.0f);
            l.timeMs = any(-10.0f, 100.0f);
            l.detuneCents = any(-10.0f, 100.0f);
            l.lowHz = any(0.0f, 50000.0f);
            l.highHz = any(0.0f, 50000.0f);
            l.note = static_cast<int>(random() % 200) - 50;
        }
        engine.setParams(p);
        for (size_t i = 0; i < x.size(); ++i) {
            const int kind = static_cast<int>(random() % 1000);
            x[i] = kind == 0 ? std::numeric_limits<float>::quiet_NaN() : kind == 1 ? std::numeric_limits<float>::infinity() : kind == 2 ? 1.0e30f : any(-1.0f, 1.0f);
            y[i] = round % 7 == 0 ? 0.7f : any(-0.5f, 0.5f); // a constant now and then
        }
        float* channels[] = { x.data(), y.data() };
        engine.process(channels, 2, 2048);
        for (size_t i = 0; i < x.size(); ++i) {
            REQUIRE(std::isfinite(x[i]));
            REQUIRE(std::isfinite(y[i]));
        }
    }
    // After a second of normal sound and normal settings, all is normal again.
    RefrenParams normal;
    engine.setParams(normal);
    auto voice = loudPhrase(kFs, 1.5, -6.0);
    float* channels[] = { voice.data() };
    engine.process(channels, 1, static_cast<int>(voice.size()));
    for (float v : voice) REQUIRE(std::isfinite(v));
    CHECK(peakDb(voice, static_cast<int>(kFs)) < 6.0);
}

TEST_CASE("audio thread never allocates") {
    RefrenParams p;
    for (auto& l : p.layer) l.on = true;
    p.layer[1].semis = 12;
    p.layer[2].source = Source::Whisper;
    p.layer[3].source = Source::Sub;
    p.grainDb = 12.0f;
    p.tilt = 0.2f;
    p.motion = Motion::Flanger;
    p.motionDepth = 0.5f;
    p.echoMix = 0.3f;
    p.verbMix = 0.3f;
    p.verbShimmer = 0.5f;
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setParams(p);
    auto x = loudPhrase(kFs, 2.0, -3.0);
    std::vector<float> scratch(x);
    AllocationCounter counter;
    for (size_t from = 0; from + 256 <= x.size(); from += 256) {
        p.layer[0].levelDb = -12.0f + static_cast<float>(from % 4096) / 1024.0f;
        p.verbDecay = 1.0f + static_cast<float>(from % 4096) / 1024.0f;
        p.echoToneHz = 2000.0f + static_cast<float>(from % 4096);
        if (from % 19200 == 0) {
            p.layer[1].semis = p.layer[1].semis == 12 ? 7 : 12;
            p.verbSize = p.verbSize == 0.5f ? 0.9f : 0.5f;
            p.grainChar = p.grainChar == Character::Lamp ? Character::Tape : Character::Lamp;
            p.motion = p.motion == Motion::Flanger ? Motion::Autopan : Motion::Flanger;
            p.echoTime = p.echoTime == EchoTime::Eighth ? EchoTime::Quarter : EchoTime::Eighth;
            p.echoMix = p.echoMix > 0.0f ? 0.0f : 0.3f;
        }
        engine.setParams(p);
        float* channels[] = { x.data() + from, scratch.data() + from };
        engine.process(channels, 2, 256);
    }
    CHECK(counter.stop() == 0);
}
