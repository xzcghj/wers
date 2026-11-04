#include "doctest.h"
#include "TestSignals.h"
#include "dsp/Space.h"
using namespace wers;
using namespace wers::test;

TEST_CASE("amount zero is bit exact") {
    Space s; s.prepare(48000); s.setParams({ 0.0f, 0.5f });
    auto x = noise(-14, 48000);
    auto y = x;
    float* ch[1] = { y.data() };
    s.process(ch, 1, 48000, 120.0);
    CHECK(y == x);
}

TEST_CASE("wet level is much lower under words than in the gap after them") {
    const double fs = 48000;
    Space s; s.prepare(fs); s.setParams({ 1.0f, 0.5f });
    auto word = vowel(150, -14, static_cast<int>(fs * 1.5), fs);
    std::vector<float> dryIn = word;
    append(dryIn, std::vector<float>(static_cast<size_t>(fs * 1.0), 0.0f));
    auto out = dryIn;
    inBlocks(out, 256, [&](float** ch, int k, int64_t) { s.process(ch, 1, k, 120.0); });
    std::vector<float> wet(out.size());
    for (size_t i = 0; i < out.size(); ++i) wet[i] = out[i] - dryIn[i];
    const int fsI = static_cast<int>(fs);
    const double under = rmsDb(wet, fsI / 2, fsI + fsI / 2 - 2000);
    const int gapStart = fsI + fsI / 2 + fsI / 7;
    const double gap = rmsDb(wet, gapStart, gapStart + fsI / 4);
    CHECK(gap > under + 8.0);
    CHECK(gap < -6.0); // never louder than the voice itself was
}

TEST_CASE("reverb decays and stays stable at the widest setting") {
    const double fs = 44100;
    Space s; s.prepare(fs); s.setParams({ 1.0f, 1.0f });
    auto x = noise(-12, static_cast<int>(fs * 0.3));
    append(x, std::vector<float>(static_cast<size_t>(fs * 12), 0.0f));
    inBlocks(x, 512, [&](float** ch, int k, int64_t) { s.process(ch, 1, k, 300.0); });
    bool ok = true; for (float v : x) ok = ok && std::isfinite(v);
    CHECK(ok);
    CHECK(rmsDb(x, static_cast<int>(fs * 11), static_cast<int>(fs * 12)) < rmsDb(x, static_cast<int>(fs * 1), static_cast<int>(fs * 2)) - 30.0);
}

TEST_CASE("tempo change and a very low tempo do not crash or read out of range") {
    Space s; s.prepare(44100); s.setParams({ 0.7f, 0.2f });
    auto x = noise(-14, 88200);
    int i = 0;
    inBlocks(x, 512, [&](float** ch, int k, int64_t) { s.process(ch, 1, k, (i++ % 2) ? 40.0 : 300.0); });
    bool ok = true; for (float v : x) ok = ok && std::isfinite(v);
    CHECK(ok);
}
