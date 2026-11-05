#include "doctest.h"
#include "TestSignals.h"
#include "dsp/Place.h"
using namespace wers;
using namespace wers::test;

namespace {
struct FakeControl : PlaceControl {
    float vocalDb = -20.0f; double from = 0, to = 0; float cut = 0; double fixFrom = 0, fixTo = 0;
    bool vocalBands(double t, float* out) const noexcept override {
        if (t < from || t >= to) return false;
        for (int i = 0; i < kCutBands; ++i) out[i] = vocalDb;
        return true;
    }
    bool fixCuts(double t, float* out) const noexcept override {
        if (t < fixFrom || t >= fixTo) return false;
        for (int i = 0; i < kCutBands; ++i) out[i] = cut;
        return true;
    }
};
}

TEST_CASE("depth zero with no fixes is bit exact") {
    Place p; p.prepare(48000); p.setDepth(0);
    FakeControl fc; fc.to = 100;
    auto l = noise(-12, 48000, 1), r = noise(-12, 48000, 2);
    auto a = l, b = r;
    float* ch[2] = { a.data(), b.data() };
    p.process(ch, 2, 48000, nullptr, 0, 0, true, &fc);
    CHECK(a == l);
    CHECK(b == r);
}

TEST_CASE("side signal and sub stay untouched while the vocal band is cut") {
    const double fs = 48000; const int n = 96000;
    Place p; p.prepare(fs); p.setDepth(1.0f);
    FakeControl fc; fc.vocalDb = -35.0f; fc.to = 100;
    auto sub = sine(60, -12, n, fs);
    auto midTone = sine(3200, -18, n, fs);
    auto sideTone = sine(3200, -18, n, fs, 1.0);
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (size_t i = 0; i < l.size(); ++i) { l[i] = sub[i] + midTone[i] + sideTone[i]; r[i] = sub[i] + midTone[i] - sideTone[i]; }
    float* ch[2] = { l.data(), r.data() };
    p.process(ch, 2, n, nullptr, 0, 0, true, &fc);
    std::vector<float> m(static_cast<size_t>(n)), s(static_cast<size_t>(n));
    for (size_t i = 0; i < m.size(); ++i) { m[i] = 0.5f * (l[i] + r[i]); s[i] = 0.5f * (l[i] - r[i]); }
    const int from = 48000, len = 48000;
    CHECK(toneDb(m.data() + from, len, 60, fs) == doctest::Approx(toneDb(sub.data() + from, len, 60, fs)).epsilon(0.02)); // within ~0.3 dB
    CHECK(rmsDb(s, from, from + len) == doctest::Approx(rmsDb(sideTone, from, from + len)).epsilon(0.002));
    CHECK(toneDb(m.data() + from, len, 3200, fs) < toneDb(midTone.data() + from, len, 3200, fs) - 3.0);
}

TEST_CASE("cut releases after the vocal ends") {
    const double fs = 48000; const int n = 96000;
    Place p; p.prepare(fs); p.setDepth(1.0f);
    FakeControl fc; fc.vocalDb = -40.0f; fc.from = 0.0; fc.to = 1.0;
    auto x = sine(3200, -18, n, fs);
    auto y = x;
    float* ch[1] = { y.data() };
    p.process(ch, 1, n, nullptr, 0, 0, true, &fc);
    CHECK(toneDb(y.data() + 30000, 12000, 3200, fs) < -21.0);
    CHECK(toneDb(y.data() + 84000, 12000, 3200, fs) == doctest::Approx(-18.0).epsilon(0.01));
}

TEST_CASE("word fix cut fires at its timeline position, even with depth zero") {
    const double fs = 48000; const int n = 96000;
    Place p; p.prepare(fs); p.setDepth(0);
    FakeControl fc; fc.cut = 6.0f; fc.fixFrom = 0.5; fc.fixTo = 0.7;
    auto x = sine(3200, -18, n, fs);
    auto y = x;
    inBlocks(y, 128, [&](float** ch, int k, int64_t pos) { p.process(ch, 1, k, nullptr, 0, pos, true, &fc); });
    CHECK(toneDb(y.data() + 12000, 9600, 3200, fs) == doctest::Approx(-18.0).epsilon(0.005));
    const double during = toneDb(y.data() + 26000, 6000, 3200, fs);
    CHECK(during < -22.0);
    CHECK(during > -32.0);
    CHECK(toneDb(y.data() + 91000, 4800, 3200, fs) == doctest::Approx(-18.0).epsilon(0.01));
}

TEST_CASE("stopped transport or unknown position never fires fixes") {
    const double fs = 48000;
    Place p; p.prepare(fs); p.setDepth(0);
    FakeControl fc; fc.cut = 9.0f; fc.fixFrom = 0.0; fc.fixTo = 100.0;
    auto x = noise(-18, 48000);
    auto a = x, b = x;
    float* c1[1] = { a.data() };
    p.process(c1, 1, 48000, nullptr, 0, 1000, false, &fc);
    CHECK(a == x);
    p.reset();
    float* c2[1] = { b.data() };
    p.process(c2, 1, 48000, nullptr, 0, -1, true, &fc);
    CHECK(b == x);
}

TEST_CASE("sidechain drives the cut when no plan is available") {
    const double fs = 48000; const int n = 96000;
    Place p; p.prepare(fs); p.setDepth(1.0f);
    auto x = sine(3200, -18, n, fs);
    auto sc = bandNoise(2000, 5000, -40, n, fs);
    auto y = x;
    float* ch[1] = { y.data() };
    const float* side[1] = { sc.data() };
    p.process(ch, 1, n, side, 1, -1, false, nullptr);
    CHECK(toneDb(y.data() + 48000, 48000, 3200, fs) < -20.0);
}

TEST_CASE("output does not depend on the block size") {
    const double fs = 44100; const int n = 44100;
    FakeControl fc; fc.vocalDb = -30.0f; fc.to = 0.5; fc.cut = 3.0f; fc.fixFrom = 0.6; fc.fixTo = 0.8;
    auto x = noise(-14, n);
    std::vector<float> ref;
    for (int block : { 1, 13, 256, 4096 }) {
        Place p; p.prepare(fs); p.setDepth(0.8f);
        auto y = x;
        inBlocks(y, block, [&](float** ch, int k, int64_t pos) { p.process(ch, 1, k, nullptr, 0, pos, true, &fc); });
        if (ref.empty()) ref = y; else CHECK(y == ref);
    }
}
