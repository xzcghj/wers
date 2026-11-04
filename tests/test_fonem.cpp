#include "doctest.h"
#include "TestSignals.h"
#include "dsp/Fonem.h"
#include "dsp/PhonemeClassifier.h"
using namespace wers;
using namespace wers::test;

namespace {
// Average membership over the last part of a signal, after settling on a loud reference vowel.
Memberships classify(const std::vector<float>& sound, double fs) {
    PhonemeClassifier c; c.prepare(fs);
    auto ref = vowel(140, -14, static_cast<int>(fs * 0.6), fs);
    for (float v : ref) c.push(v);
    Memberships sum; sum.vowel = sum.sib = sum.stop = sum.breath = sum.silence = 0;
    int hops = 0;
    for (size_t i = 0; i < sound.size(); ++i) {
        if (c.push(sound[i]) && i > sound.size() / 4) {
            const auto& m = c.current();
            sum.vowel += m.vowel; sum.sib += m.sib; sum.stop += m.stop; sum.breath += m.breath; sum.silence += m.silence;
            sum.sibCentroidHz = m.sibCentroidHz;
            ++hops;
        }
    }
    const float n = static_cast<float>(std::max(1, hops));
    sum.vowel /= n; sum.sib /= n; sum.stop /= n; sum.breath /= n; sum.silence /= n;
    return sum;
}
}

TEST_CASE("classifier recognises the main sound classes at several sample rates") {
    for (double fs : { 16000.0, 44100.0, 48000.0, 96000.0 }) {
        const int n = static_cast<int>(fs * 0.2);
        CAPTURE(fs);
        const auto v = classify(vowel(180, -16, n, fs), fs);
        CHECK(v.vowel > 0.7f);
        CHECK(v.sib < 0.1f);
        const double top = std::min(9500.0, fs * 0.46);
        const auto s = classify(bandNoise(5200, top, -22, n, fs), fs);
        CHECK(s.sib > 0.6f);
        CHECK(s.vowel < 0.1f);
        const auto sz = classify(bandNoise(2900, 5000, -22, n, fs), fs);
        CHECK(sz.sib > 0.5f);
        const auto b = classify(bandNoise(400, 6000, -46, n, fs), fs);
        CHECK(b.breath > 0.5f);
        CHECK(b.vowel < 0.15f);
        const auto q = classify(std::vector<float>(static_cast<size_t>(n), 0.0f), fs);
        CHECK(q.silence > 0.9f);
    }
}

TEST_CASE("sibilant centre separates sz, si and s") {
    const double fs = 48000;
    const int n = 9600;
    const float sz = classify(bandNoise(2900, 4600, -22, n, fs), fs).sibCentroidHz;
    const float si = classify(bandNoise(4300, 6400, -22, n, fs), fs).sibCentroidHz;
    const float s = classify(bandNoise(6500, 10500, -22, n, fs), fs).sibCentroidHz;
    CHECK(sz < si);
    CHECK(si < s);
}

TEST_CASE("burst after a gap is detected as a stop") {
    const double fs = 48000;
    PhonemeClassifier c; c.prepare(fs);
    auto ref = vowel(140, -14, 28800, fs);
    for (float v : ref) c.push(v);
    for (int i = 0; i < 4800; ++i) c.push(0.0f);
    auto burst = noise(-20, 960);
    float peak = 0;
    for (float v : burst) { c.push(v); peak = std::max(peak, c.current().stop); }
    CHECK(peak > 0.6f);
}

TEST_CASE("fonem at zero is a pure delay of the reported latency") {
    for (double fs : { 44100.0, 48000.0, 96000.0 }) {
        Fonem f; f.prepare(fs);
        f.setParams({ 0, 0, 0, 0, 0.5f });
        auto x = noise(-18, static_cast<int>(fs));
        auto y = x;
        inBlocks(y, 256, [&](float** ch, int n, int64_t) { f.process(ch, 1, n); });
        const int lat = f.latencySamples();
        CHECK(lat < static_cast<int>(fs * 0.01));
        bool same = true;
        for (size_t i = static_cast<size_t>(lat); i < x.size(); ++i) same = same && y[i] == x[i - static_cast<size_t>(lat)];
        CHECK(same);
    }
}

TEST_CASE("fonem output does not depend on the block size") {
    const double fs = 48000;
    auto x = vowel(150, -16, 24000, fs);
    append(x, bandNoise(5000, 9000, -24, 12000, fs));
    append(x, vowel(190, -14, 24000, fs));
    const FonemParams p{ 0.7f, 0.8f, 0.4f, 0.6f, 0.5f };
    std::vector<float> ref;
    for (int block : { 1, 7, 64, 512, 4096 }) {
        Fonem f; f.prepare(fs); f.setParams(p);
        auto y = x;
        inBlocks(y, block, [&](float** ch, int n, int64_t) { f.process(ch, 1, n); });
        if (ref.empty()) ref = y; else CHECK(y == ref);
    }
}

TEST_CASE("saturation lands on vowels and leaves sibilants clean") {
    const double fs = 48000;
    const int n = 48000;
    Fonem on, off;
    on.prepare(fs); off.prepare(fs);
    on.setParams({ 0.9f, 0, 0, 0, 1.0f });
    off.setParams({ 0, 0, 0, 0, 1.0f });
    auto ref = vowel(140, -14, n / 2, fs);

    // A pure tone stands in for a vowel: its harmonics are easy to measure.
    auto tone = sine(220, -14, n, fs);
    auto sib = bandNoise(5500, 9000, -24, n / 2, fs);
    std::vector<float> sig = ref;
    append(sig, tone);
    append(sig, sib);
    auto a = sig, b = sig;
    inBlocks(a, 512, [&](float** ch, int k, int64_t) { on.process(ch, 1, k); });
    inBlocks(b, 512, [&](float** ch, int k, int64_t) { off.process(ch, 1, k); });

    const int toneStart = n / 2 + 12000, toneLen = 24000;
    const double fund = toneDb(a.data() + toneStart, toneLen, 220, fs);
    const double h2 = toneDb(a.data() + toneStart, toneLen, 440, fs);
    const double h3 = toneDb(a.data() + toneStart, toneLen, 660, fs);
    CHECK(std::max(h2, h3) > fund - 50.0); // audible harmonics on the vowel
    const double h2off = toneDb(b.data() + toneStart, toneLen, 440, fs);
    CHECK(h2off < fund - 90.0);

    // Sibilant part: the noise has no energy below 3 kHz, so anything there is a distortion product.
    const int sibStart = n + n / 2 + 4000;
    const int sibLen = 16000;
    const auto lowPart = [&](const std::vector<float>& y) {
        Svf l1, l2; l1.set(2500, 0.7, fs); l2.set(2500, 0.7, fs);
        std::vector<float> o(static_cast<size_t>(sibLen));
        for (int i = 0; i < sibLen; ++i) o[static_cast<size_t>(i)] = static_cast<float>(l2.tick(l1.tick(y[static_cast<size_t>(sibStart + i)]).lp).lp);
        return rmsDb(o, 4000);
    };
    CHECK(lowPart(a) < lowPart(b) + 6.0);
}

TEST_CASE("clarity reduces harsh sibilants and leaves vowels alone") {
    const double fs = 48000;
    auto sig = vowel(150, -14, 28800, fs);
    const int sibStart = static_cast<int>(sig.size());
    append(sig, bandNoise(5500, 9500, -12, 12000, fs)); // sibilant as loud as the vowel: harsh
    const int vowelStart = static_cast<int>(sig.size());
    append(sig, vowel(150, -14, 28800, fs));
    Fonem on, off; on.prepare(fs); off.prepare(fs);
    on.setParams({ 0, 1.0f, 0, 0, 0.5f }); off.setParams({ 0, 0, 0, 0, 0.5f });
    auto a = sig, b = sig;
    inBlocks(a, 256, [&](float** c, int k, int64_t) { on.process(c, 1, k); });
    inBlocks(b, 256, [&](float** c, int k, int64_t) { off.process(c, 1, k); });
    const int L = on.latencySamples();
    CHECK(rmsDb(a, sibStart + L + 2000, sibStart + L + 11000) < rmsDb(b, sibStart + L + 2000, sibStart + L + 11000) - 3.0);
    CHECK(rmsDb(a, vowelStart + L + 8000, vowelStart + L + 28000) == doctest::Approx(rmsDb(b, vowelStart + L + 8000, vowelStart + L + 28000)).epsilon(0.02));
}

TEST_CASE("breath control lowers breaths") {
    const double fs = 48000;
    auto sig = vowel(150, -14, 28800, fs);
    const int bStart = static_cast<int>(sig.size());
    append(sig, bandNoise(400, 6000, -46, 19200, fs));
    Fonem on, off; on.prepare(fs); off.prepare(fs);
    on.setParams({ 0, 0, 0, 1.0f, 0.5f }); off.setParams({ 0, 0, 0, 0, 0.5f });
    auto a = sig, b = sig;
    inBlocks(a, 256, [&](float** c, int k, int64_t) { on.process(c, 1, k); });
    inBlocks(b, 256, [&](float** c, int k, int64_t) { off.process(c, 1, k); });
    const int L = on.latencySamples();
    CHECK(rmsDb(a, bStart + L + 9000, bStart + L + 18800) < rmsDb(b, bStart + L + 9000, bStart + L + 18800) - 6.0);
}

TEST_CASE("fonem survives extreme input and settings without NaN") {
    const double fs = 44100;
    Fonem f; f.prepare(fs);
    f.setParams({ 1, 1, 1, 1, 1 });
    Lcg r;
    std::vector<float> x(44100);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (i % 3000 < 400) ? r.next() * 40.0f : (i % 7000 < 100 ? 0.0f : r.next() * 1e-20f);
    x[5000] = 1e6f;
    inBlocks(x, 333, [&](float** c, int k, int64_t) { f.process(c, 1, k); });
    bool ok = true; for (float v : x) ok = ok && std::isfinite(v);
    CHECK(ok);
}
