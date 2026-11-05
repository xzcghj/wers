#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Crossover.h"
#include "Fft.h"
#include "LevelMatch.h"
#include "Measure.h"
#include "Oversampler.h"
#include "TestSignals.h"
#include <complex>

using namespace wers;
using namespace wers::test;

namespace {
std::vector<float> throughOversampler(const std::vector<float>& x, int factorLog2) {
    Oversampler os;
    os.prepare(factorLog2);
    std::vector<float> y(x.size());
    std::array<float, 8> high{};
    for (size_t i = 0; i < x.size(); ++i) {
        os.up(x[i], high.data());
        y[i] = os.down(high.data());
    }
    return y;
}

// The response of a split, read from its impulse response.
template <typename Fn>
std::vector<double> responseDb(Fn&& oneSample, int size) {
    std::vector<std::complex<double>> h(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) h[static_cast<size_t>(i)] = oneSample(i == 0 ? 1.0f : 0.0f);
    Fft(size).transform(h);
    std::vector<double> db(static_cast<size_t>(size / 2));
    for (size_t i = 0; i < db.size(); ++i) db[i] = 20.0 * std::log10(std::abs(h[i]) + 1e-20);
    return db;
}
} // namespace

TEST_CASE("factor stays within 384 kHz") {
    CHECK(effectiveFactorLog2(3, 48000.0) == 3);
    CHECK(effectiveFactorLog2(3, 96000.0) == 2);
    CHECK(effectiveFactorLog2(3, 192000.0) == 1);
    CHECK(effectiveFactorLog2(1, 192000.0) == 1);
    CHECK(effectiveFactorLog2(2, 44100.0) == 2);
    CHECK(effectiveFactorLog2(3, 88200.0) == 2);
}

TEST_CASE("oversampler latency is whole and exact") {
    const double fs = 48000.0;
    const auto x = sine(1000.0, -6.0, 48000, fs);
    for (int log2 : { 1, 2, 3 }) {
        CAPTURE(log2);
        Oversampler os;
        os.prepare(log2);
        CHECK(os.factor() == 1 << log2);
        const int latency = os.latencySamples();
        CHECK(latency == (log2 == 1 ? 49 : (log2 == 2 ? 57 : 59)));
        const auto y = throughOversampler(x, log2);
        double error = 0.0, signal = 0.0;
        for (size_t i = 2000; i < x.size(); ++i) {
            const double d = static_cast<double>(y[i]) - x[i - static_cast<size_t>(latency)];
            error += d * d;
            signal += static_cast<double>(x[i]) * x[i];
        }
        CHECK(10.0 * std::log10(error / signal) < -80.0);
    }
}

TEST_CASE("oversampler keeps the audible band") {
    for (double fs : { 44100.0, 48000.0 }) {
        for (int log2 : { 1, 2, 3 }) {
            CAPTURE(fs); CAPTURE(log2);
            const auto x = sine(18000.0, -6.0, 32768, fs);
            auto y = throughOversampler(x, log2);
            y.erase(y.begin(), y.begin() + 2000);
            std::vector<float> reference(x.begin() + 2000, x.end());
            CHECK(std::abs(magnitudeAtDb(reference, y, 18000.0, fs)) < 0.05);
        }
    }
}

TEST_CASE("oversampler rejects what would fold back") {
    const double fs = 48000.0;
    for (int log2 : { 1, 2, 3 }) {
        CAPTURE(log2);
        Oversampler os;
        os.prepare(log2);
        const int factor = os.factor();
        const double high = fs * factor;
        // 30 kHz can only exist above the base rate. Going down, it must not fold to 18 kHz.
        std::vector<float> out(32768);
        std::array<float, 8> frame{};
        for (size_t i = 0; i < out.size(); ++i) {
            for (int k = 0; k < factor; ++k)
                frame[static_cast<size_t>(k)] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 30000.0 * (static_cast<double>(i) * factor + k) / high));
            out[i] = os.down(frame.data());
        }
        out.erase(out.begin(), out.begin() + 2000);
        CHECK(toneLevelDb(out, 18000.0, fs) < -6.0 - 95.0);
    }
}

TEST_CASE("oversampler leaves no images going up") {
    const double fs = 48000.0;
    for (int log2 : { 1, 2, 3 }) {
        CAPTURE(log2);
        Oversampler os;
        os.prepare(log2);
        const int factor = os.factor();
        const double high = fs * factor;
        std::vector<float> stream;
        std::array<float, 8> frame{};
        const auto x = sine(10000.0, -6.0, 16384, fs);
        for (float v : x) {
            os.up(v, frame.data());
            stream.insert(stream.end(), frame.begin(), frame.begin() + factor);
        }
        stream.erase(stream.begin(), stream.begin() + 2000 * factor);
        CHECK(toneLevelDb(stream, fs - 10000.0, high) < -6.0 - 95.0);
    }
}

TEST_CASE("crossover sum is flat") {
    const std::array<std::pair<float, float>, 4> pairs{ { { 60.0f, 1000.0f }, { 200.0f, 3000.0f }, { 1000.0f, 1000.0f }, { 1000.0f, 10000.0f } } };
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (int bands : { 1, 2, 3 }) {
            for (const auto& [f1, f2] : pairs) {
                CAPTURE(fs); CAPTURE(bands); CAPTURE(f1); CAPTURE(f2);
                Crossover3 c;
                c.prepare(fs);
                c.setBands(bands);
                c.setFrequencies(f1, f2);
                constexpr int kSize = 65536;
                const auto db = responseDb([&](float x) {
                    float low, mid, high;
                    c.process(x, low, mid, high);
                    return static_cast<double>(low) + mid + high;
                }, kSize);
                double worst = 0.0;
                for (size_t bin = 0; bin < db.size(); ++bin) {
                    const double hz = static_cast<double>(bin) * fs / kSize;
                    if (hz >= 20.0 && hz <= 20000.0) worst = std::max(worst, std::abs(db[bin]));
                }
                CHECK(worst < 0.05);
            }
        }
    }
}

TEST_CASE("crossover bands really separate") {
    const double fs = 48000.0;
    Crossover3 c;
    c.prepare(fs);
    c.setBands(3);
    c.setFrequencies(200.0f, 3000.0f);
    constexpr int kSize = 65536;
    std::array<std::vector<std::complex<double>>, 3> parts;
    for (auto& p : parts) p.resize(kSize);
    for (int i = 0; i < kSize; ++i) {
        float low, mid, high;
        c.process(i == 0 ? 1.0f : 0.0f, low, mid, high);
        parts[0][static_cast<size_t>(i)] = low;
        parts[1][static_cast<size_t>(i)] = mid;
        parts[2][static_cast<size_t>(i)] = high;
    }
    Fft fft(kSize);
    for (auto& p : parts) fft.transform(p);
    const auto levelDb = [&](int part, double hz) { return 20.0 * std::log10(std::abs(parts[static_cast<size_t>(part)][static_cast<size_t>(hz * kSize / fs)]) + 1e-20); };
    CHECK(levelDb(0, 400.0) < -20.0);
    CHECK(levelDb(1, 100.0) < -20.0);
    CHECK(levelDb(1, 6000.0) < -20.0);
    CHECK(levelDb(2, 1500.0) < -20.0);
    CHECK(levelDb(1, 800.0) > -1.0);
}

TEST_CASE("one band is the input itself") {
    Crossover3 c;
    c.prepare(48000.0);
    c.setBands(1);
    c.setFrequencies(200.0f, 3000.0f);
    const auto x = noise(-12.0, 4800);
    for (float v : x) {
        float low, mid, high;
        c.process(v, low, mid, high);
        REQUIRE(low == v);
        REQUIRE(mid == 0.0f);
        REQUIRE(high == 0.0f);
    }
}

TEST_CASE("moving the split stays stable") {
    const double fs = 48000.0;
    const auto x = noise(-12.0, static_cast<int>(fs * 5.0));
    // Turning the phase of white noise raises its peaks by itself, so the peak with fixed splits is the
    // reference. A filter that burst while moving would stand out from it.
    const auto peakOf = [&](auto&& moveSplit) {
        Crossover3 c;
        c.prepare(fs);
        c.setBands(3);
        float peak = 0.0f;
        for (size_t i = 0; i < x.size(); ++i) {
            if (i % 32 == 0) moveSplit(c, static_cast<double>(i) / static_cast<double>(x.size()));
            float low, mid, high;
            c.process(x[i], low, mid, high);
            const float sum = low + mid + high;
            REQUIRE(std::isfinite(sum));
            peak = std::max(peak, std::abs(sum));
        }
        return peak;
    };
    float fixedPeak = 0.0f;
    for (const auto& [f1, f2] : std::array<std::pair<float, float>, 3>{ { { 60.0f, 1000.0f }, { 200.0f, 3000.0f }, { 1000.0f, 10000.0f } } })
        fixedPeak = std::max(fixedPeak, peakOf([f1 = f1, f2 = f2](Crossover3& c, double) { c.setFrequencies(f1, f2); }));
    const float movingPeak = peakOf([](Crossover3& c, double t) {
        c.setFrequencies(static_cast<float>(60.0 + 940.0 * std::abs(std::sin(t * 37.0))), static_cast<float>(1000.0 + 9000.0 * std::abs(std::sin(t * 23.0))));
    });
    MESSAGE("peak with fixed splits " << gainToDb(fixedPeak) << " dB, moving " << gainToDb(movingPeak) << " dB");
    CHECK(gainToDb(movingPeak / fixedPeak) < 1.0f);
}

TEST_CASE("level match corrects a steady difference") {
    const double fs = 48000.0;
    const auto x = noise(-20.0, static_cast<int>(fs * 2.0));
    const auto settle = [&](float wetGain) {
        LevelMatch match;
        match.prepare(fs);
        float g = 1.0f;
        for (float v : x) g = match.push(v, v * wetGain);
        return g;
    };
    CHECK(std::abs(gainToDb(settle(0.5f)) - 6.02) < 0.3);
    CHECK(std::abs(gainToDb(settle(2.0f)) + 6.02) < 0.3);
    CHECK(std::abs(gainToDb(settle(0.1f)) - 12.0) < 0.1);
    CHECK(settle(1.0f) == 1.0f);
}

TEST_CASE("level match hears both channels") {
    const double fs = 48000.0;
    const auto x = noise(-20.0, static_cast<int>(fs * 2.0));
    LevelMatch stereo, mono;
    stereo.prepare(fs);
    mono.prepare(fs);
    float both = 1.0f, single = 1.0f;
    for (float v : x) {
        // Opposite channels have nothing in their sum, and what differs is still only 6 dB.
        both = stereo.push(v, -v, 0.5f * v, -0.5f * v);
        single = mono.push(v, 0.5f * v);
    }
    CHECK(std::abs(gainToDb(both) - 6.02) < 0.3);
    CHECK(both == single);
    // One channel silent: the other one still counts.
    LevelMatch oneSide;
    oneSide.prepare(fs);
    float side = 1.0f;
    for (float v : x) side = oneSide.push(v, 0.0f, 0.5f * v, 0.0f);
    CHECK(std::abs(gainToDb(side) - 6.02) < 0.3);
}

TEST_CASE("level match holds in silence") {
    const double fs = 48000.0;
    LevelMatch match;
    match.prepare(fs);
    const auto x = noise(-20.0, static_cast<int>(fs * 3.0));
    float held = 1.0f;
    for (float v : x) held = match.push(v, v * 0.5f);
    float g = held;
    for (int i = 0; i < static_cast<int>(fs * 5.0); ++i) g = match.push(0.0f, 0.0f);
    CHECK(std::abs(gainToDb(g) - gainToDb(held)) < 0.3f);
    for (int i = 0; i < static_cast<int>(fs * 5.0); ++i) g = match.push(1e-5f, 1e-3f);
    CHECK(std::abs(gainToDb(g) - gainToDb(held)) < 0.3f);
}
