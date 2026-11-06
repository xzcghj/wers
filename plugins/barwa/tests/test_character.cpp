#include "doctest.h"
#include "CharacterShaper.h"
#include "Measure.h"
#include "Oversampler.h"
#include "TestSignals.h"
#include <limits>

using namespace wers;
using namespace wers::test;

namespace {

constexpr std::array<Character, 4> kCharacters{ Character::Lamp, Character::Tape, Character::Transformer, Character::Transistor };

// A sine through oversampling and the shaper, without level matching and without the part where filters settle.
std::vector<float> runShaped(Character character, double hz, double inDb, double driveDb, double fs, int qualityLog2) {
    Oversampler os;
    os.prepare(effectiveFactorLog2(qualityLog2, fs));
    CharacterShaper shaper;
    shaper.prepare(fs * os.factor());
    shaper.setCharacter(character);
    const float gain = dbToGain(static_cast<float>(driveDb));
    const int settle = static_cast<int>(fs * 0.25);
    const auto x = sine(hz, inDb, 65536 + settle, fs);
    std::vector<float> y(x.size());
    std::array<float, 8> frame{};
    for (size_t i = 0; i < x.size(); ++i) {
        os.up(x[i], frame.data());
        for (int k = 0; k < os.factor(); ++k) frame[static_cast<size_t>(k)] = shaper.tick(frame[static_cast<size_t>(k)], gain);
        y[i] = os.down(frame.data());
    }
    y.erase(y.begin(), y.begin() + settle);
    return y;
}

} // namespace

TEST_CASE("small signals pass unchanged") {
    for (Character character : kCharacters) {
        for (double hz : { 100.0, 1000.0, 8000.0 }) {
            CAPTURE(static_cast<int>(character)); CAPTURE(hz);
            const auto y = runShaped(character, hz, -60.0, 12.0, 48000.0, 2);
            CHECK(std::abs(harmonicDb(y, hz, 1, 48000.0) + 60.0) < 0.1);
        }
    }
}

TEST_CASE("lamp favours the second harmonic") {
    for (double fs : kRates) {
        CAPTURE(fs);
        const auto y = runShaped(Character::Lamp, 1000.0, -18.0, 12.0, fs, 2);
        const double second = harmonicDb(y, 1000.0, 2, fs), third = harmonicDb(y, 1000.0, 3, fs);
        MESSAGE("lamp at " << fs << ": H2 " << second << " dB, H3 " << third << " dB");
        CHECK(second >= third + 3.0);
    }
}

TEST_CASE("tape saturates highs first") {
    // At the lower rates the third harmonic of 8 kHz would be out of band, so the test tone is lower there.
    for (double fs : kRates) {
        CAPTURE(fs);
        const double high = fs >= 80000.0 ? 8000.0 : 6000.0;
        const double highThd = thdDb(runShaped(Character::Tape, high, -18.0, 12.0, fs, 2), high, fs);
        const double midThd = thdDb(runShaped(Character::Tape, 1000.0, -18.0, 12.0, fs, 2), 1000.0, fs);
        MESSAGE("tape at " << fs << ": THD " << high << " Hz " << highThd << " dB, 1 kHz " << midThd << " dB");
        CHECK(highThd >= midThd + 6.0);
    }
}

TEST_CASE("tape follows its curve") {
    // The curve written out with the plain power function, which is too slow to use in the sound.
    constexpr double kShelfHz = 3000.0, kShelfDb = 6.0, kFlatQ = 0.70710678118654752;
    for (double fs : { 88200.0, 96000.0, 192000.0 }) {
        for (float gain : { 1.0f, 4.0f, 16.0f, 63.0f }) {
            for (double level : { -60.0, -24.0, -6.0, 12.0 }) {
                CAPTURE(fs); CAPTURE(gain); CAPTURE(level);
                CharacterShaper shaper;
                shaper.prepare(fs);
                shaper.setCharacter(Character::Tape);
                wers::Biquad emphasis, deemphasis;
                emphasis.set(wers::Biquad::Type::HighShelf, kShelfHz, kFlatQ, kShelfDb, fs);
                deemphasis.set(wers::Biquad::Type::HighShelf, kShelfHz, kFlatQ, -kShelfDb, fs);
                double error = 0.0, signal = 0.0;
                for (float x : noise(level, 48000)) {
                    const float u = gain * static_cast<float>(emphasis.tick(static_cast<double>(x)));
                    const float a = std::abs(u);
                    const float y = u / std::pow(1.0f + a * a * std::sqrt(a), 0.4f);
                    const double wanted = deemphasis.tick(static_cast<double>(y / gain));
                    const double d = static_cast<double>(shaper.tick(x, gain)) - wanted;
                    error += d * d;
                    signal += wanted * wanted;
                }
                CHECK(10.0 * std::log10((error + 1.0e-30) / signal) < -110.0);
            }
        }
    }
}

TEST_CASE("transformer saturates lows first") {
    for (double fs : kRates) {
        CAPTURE(fs);
        const double lowThd = thdDb(runShaped(Character::Transformer, 100.0, -18.0, 12.0, fs, 2), 100.0, fs);
        const double midThd = thdDb(runShaped(Character::Transformer, 1000.0, -18.0, 12.0, fs, 2), 1000.0, fs);
        MESSAGE("transformer at " << fs << ": THD 100 Hz " << lowThd << " dB, 1 kHz " << midThd << " dB");
        CHECK(lowThd >= midThd + 6.0);
    }
}

TEST_CASE("transistor favours the third harmonic") {
    for (double fs : kRates) {
        CAPTURE(fs);
        const auto y = runShaped(Character::Transistor, 1000.0, -18.0, 12.0, fs, 2);
        const double second = harmonicDb(y, 1000.0, 2, fs), third = harmonicDb(y, 1000.0, 3, fs);
        MESSAGE("transistor at " << fs << ": H2 " << second << " dB, H3 " << third << " dB");
        CHECK(third >= second + 12.0);
    }
}

TEST_CASE("aliases stay down") {
    const double fs = 48000.0;
    for (Character character : kCharacters) {
        const std::array<std::pair<int, double>, 3> targets{ { { 1, -60.0 }, { 2, -80.0 }, { 3, -90.0 } } };
        for (const auto& [quality, limit] : targets) {
            CAPTURE(static_cast<int>(character)); CAPTURE(quality);
            const auto y = runShaped(character, 5000.0, -6.0, 12.0, fs, quality);
            const double alias = strongestNonHarmonicDb(y, 5000.0, fs);
            MESSAGE("character " << static_cast<int>(character) << " at factor " << (1 << quality) << ": alias " << alias << " dB");
            CHECK(alias <= limit);
        }
    }
}

TEST_CASE("shapers survive extremes") {
    const double fs = 48000.0;
    for (Character character : kCharacters) {
        CAPTURE(static_cast<int>(character));
        Oversampler os;
        os.prepare(2);
        CharacterShaper shaper;
        shaper.prepare(fs * os.factor());
        shaper.setCharacter(character);
        const float gain = dbToGain(36.0f);
        std::array<float, 8> frame{};
        const auto oneSample = [&](float x) {
            os.up(x, frame.data());
            for (int k = 0; k < os.factor(); ++k) {
                frame[static_cast<size_t>(k)] = shaper.tick(frame[static_cast<size_t>(k)], gain);
                REQUIRE(std::isfinite(frame[static_cast<size_t>(k)]));
            }
            return os.down(frame.data());
        };
        // NaN and Inf can reach a filter only through the shaper's input; the oversampler is tested with clean values.
        const std::array<float, 6> wild{ 10.0f, -10.0f, 1.0e30f, -1.0e30f, 1.0e-30f, 0.0f };
        for (int i = 0; i < 48000; ++i) REQUIRE(std::isfinite(oneSample(wild[static_cast<size_t>(i) % wild.size()])));
        for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
            for (int i = 0; i < 64; ++i) REQUIRE(std::isfinite(shaper.tick(bad, gain)));
        }
        // After a second of ordinary signal everything is back to normal.
        for (int i = 0; i < 48000; ++i) oneSample(0.0f);
        const auto x = sine(1000.0, -60.0, 48000, fs);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = oneSample(x[i]);
        y.erase(y.begin(), y.begin() + 12000);
        CHECK(std::abs(toneLevelDb(y, 1000.0, fs) + 60.0) < 0.5);
    }
}
