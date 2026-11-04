#include "doctest.h"
#include "TestSignals.h"
#include "dsp/Monitor.h"
using namespace wers;
using namespace wers::test;

TEST_CASE("997 Hz sine at -20 dBFS in both channels reads -20 LUFS at common rates") {
    for (double fs : { 44100.0, 48000.0, 96000.0, 16000.0 }) {
        LoudnessMeter m; m.prepare(fs);
        auto x = sine(997, -20, static_cast<int>(fs * 6), fs);
        const float* ch[2] = { x.data(), x.data() };
        m.process(ch, 2, static_cast<int>(x.size()));
        CHECK(m.integratedLufs() == doctest::Approx(-20.0).epsilon(0.01));
        CHECK(m.momentaryLufs() == doctest::Approx(-20.0).epsilon(0.01));
    }
}

TEST_CASE("true peak finds a peak between samples") {
    LoudnessMeter m; m.prepare(48000);
    auto x = sine(12000, 0, 48000, 48000, kPi / 4.0); // samples at 0.707, real peak at 1.0
    const float* ch[1] = { x.data() };
    m.process(ch, 1, static_cast<int>(x.size()));
    CHECK(m.truePeakDb() == doctest::Approx(0.0).epsilon(0.5));
}

TEST_CASE("relative gating ignores quiet passages") {
    LoudnessMeter m; m.prepare(48000);
    auto x = sine(997, -20, 48000 * 10, 48000);
    append(x, sine(997, -50, 48000 * 10, 48000));
    const float* ch[2] = { x.data(), x.data() };
    m.process(ch, 2, static_cast<int>(x.size()));
    CHECK(m.integratedLufs() == doctest::Approx(-20.0).epsilon(0.02));
}

TEST_CASE("Spotify rule from their documentation") {
    CHECK(spotifyGainDb(-20.0, -5.0, NormMode::Normal14) == doctest::Approx(4.0)); // lifted only to -16 LUFS
    CHECK(spotifyGainDb(-9.0, -0.5, NormMode::Normal14) == doctest::Approx(-5.0));
    CHECK(spotifyGainDb(-20.0, -5.0, NormMode::Loud11) == doctest::Approx(9.0));
    CHECK(spotifyGainDb(-14.0, -1.0, NormMode::Quiet19) == doctest::Approx(-5.0));
    CHECK(spotifyGainDb(-14.0, -1.0, NormMode::Off) == doctest::Approx(0.0));
}

TEST_CASE("device simulation changes realtime playback but never an offline render") {
    Monitor mon; mon.prepare(48000);
    mon.setDevice(Device::Phone);
    auto x = sine(80, -12, 48000, 48000);
    auto rt = x, off = x;
    float* c1[1] = { rt.data() }; float* c2[1] = { off.data() };
    mon.process(c1, 1, static_cast<int>(rt.size()), false);
    CHECK(rmsDb(rt, 24000) < rmsDb(x, 24000) - 20.0); // phone removes 80 Hz
    mon.reset();
    mon.process(c2, 1, static_cast<int>(off.size()), true);
    CHECK(off == x);
}

TEST_CASE("switching the device while the song plays does not click") {
    const double fs = 48000;
    for (Device to : { Device::Phone, Device::Car, Device::Studio }) {
        Monitor mon; mon.prepare(fs);
        mon.setDevice(Device::Earbuds);
        auto x = sine(440, -12, 96000, fs);
        int block = 0;
        inBlocks(x, 256, [&](float** c, int k, int64_t) {
            if (++block == 150) mon.setDevice(to);
            mon.process(c, 1, k, false);
        });
        float biggest = 0.0f;
        for (size_t i = 4800; i + 1 < x.size(); ++i) biggest = std::max(biggest, std::abs(x[i + 1] - x[i]));
        CHECK(biggest < 0.03f); // a 440 Hz tone at this level moves by about 0.015 per sample
    }
}

TEST_CASE("studio monitor with no normalisation leaves audio bit exact") {
    Monitor mon; mon.prepare(44100);
    mon.setDevice(Device::Studio);
    auto x = noise(-12, 44100);
    auto y = x;
    float* c[1] = { y.data() };
    mon.process(c, 1, static_cast<int>(y.size()), false);
    CHECK(y == x);
}

TEST_CASE("model curves match the audio filters") {
    for (int d = 0; d < kNumDevices; ++d) {
        DeviceFilter f; f.prepare(static_cast<Device>(d), 48000);
        const auto& b = deviceBands(static_cast<Device>(d));
        for (int k = 0; k < kSiiBands; ++k)
            CHECK(std::abs(f.magnitudeDb(kSiiFreq[static_cast<size_t>(k)], 48000) - b.gainDb[static_cast<size_t>(k)]) < 0.3);
    }
}
