#include "doctest.h"
#include "AllocationCount.h"
#include "Measure.h"
#include "TestSignals.h"
#include "VoiceAnalysis.h"
#include "VoiceSignals.h"
#include <map>

using namespace refren;
using namespace refren::test;
using namespace wers::test;

WERS_COUNT_ALLOCATIONS()

namespace {

struct Share {
    int total = 0, voiced = 0, close = 0;
    double closeShare() const { return voiced > 0 ? static_cast<double>(close) / voiced : 0.0; }
    double voicedShare() const { return total > 0 ? static_cast<double>(voiced) / total : 0.0; }
};

// Pushes x and looks up each reading after it has settled, against the pitch the sound has at that moment.
template <typename Truth>
Share readAll(const std::vector<float>& x, double fs, double fromSeconds, double cents, Truth&& truthHz) {
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    Share share;
    const int hop = static_cast<int>(fs * 0.0025), lag = static_cast<int>(fs * 0.05);
    for (size_t i = 0; i < x.size(); ++i) {
        analysis.push(x[i]);
        const int64_t now = analysis.now();
        if (now % hop == 0 && now - lag >= static_cast<int64_t>(fs * fromSeconds)) {
            const int64_t t = now - lag;
            const auto reading = analysis.pitchAt(t);
            ++share.total;
            if (reading.voiced) {
                ++share.voiced;
                const double hz = fs / reading.period;
                if (std::abs(centsBetween(hz, truthHz(static_cast<double>(t) / fs))) <= cents) ++share.close;
            }
        }
    }
    return share;
}

std::vector<int64_t> collectMarks(const std::vector<float>& x, double fs) {
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    std::map<int64_t, bool> seen;
    for (float v : x) {
        analysis.push(v);
        Mark mark;
        const int64_t now = analysis.now();
        if (now % 16 == 0 && analysis.markNear(now, now, mark)) seen[mark.position] = true;
    }
    std::vector<int64_t> positions;
    for (const auto& [position, unused] : seen) positions.push_back(position);
    return positions;
}

} // namespace

TEST_CASE("pitch of a steady voice") {
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (double f0 : { 80.0, 90.0, 150.0, 250.0, 400.0, 800.0 }) {
            CAPTURE(fs); CAPTURE(f0);
            const auto share = readAll(vowel(f0, -18.0, static_cast<int>(fs), fs), fs, 0.1, 5.0, [&](double) { return f0; });
            CHECK(share.voicedShare() >= 0.95);
            CHECK(share.closeShare() >= 0.95);
        }
    }
}

TEST_CASE("pitch through a glide") {
    const double length = 2.0;
    for (double fs : { 44100.0, 48000.0 }) {
        for (const auto& glide : std::array<std::pair<double, double>, 2>{ { { 110.0, 220.0 }, { 300.0, 150.0 } } }) {
            const double from = glide.first, to = glide.second;
            CAPTURE(fs); CAPTURE(from);
            const auto x = glideVoice(from, to, -18.0, static_cast<int>(fs * length), fs);
            const auto share = readAll(x, fs, 0.1, 10.0, [from, to, length](double t) { return glideHz(from, to, t, length); });
            MESSAGE("glide from " << from << ": " << share.closeShare() * 100.0 << "% within 10 cents");
            CHECK(share.voicedShare() >= 0.95);
            CHECK(share.closeShare() >= 0.9);
        }
    }
}

TEST_CASE("noise has no pitch") {
    const double fs = 48000.0;
    const int n = static_cast<int>(fs * 2.0);
    CHECK(readAll(noise(-18.0, n), fs, 0.1, 5.0, [](double) { return 1.0; }).voicedShare() <= 0.02);
    CHECK(readAll(bandNoise(4000.0, 9000.0, -18.0, n, fs), fs, 0.1, 5.0, [](double) { return 1.0; }).voicedShare() <= 0.02);
}

TEST_CASE("silence has no pitch") {
    const double fs = 48000.0;
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    for (int i = 0; i < static_cast<int>(fs); ++i) analysis.push(0.0f);
    const auto reading = analysis.pitchAt(analysis.now() - static_cast<int64_t>(fs * 0.05));
    CHECK_FALSE(reading.voiced);
    CHECK(std::isfinite(reading.confidence));
    Mark mark;
    CHECK_FALSE(analysis.markNear(analysis.now(), analysis.now(), mark));
}

TEST_CASE("level does not change the reading") {
    const double fs = 48000.0;
    const auto meanCents = [&](double db) {
        const auto x = vowel(150.0, db, static_cast<int>(fs), fs);
        VoiceAnalysis analysis;
        analysis.prepare(fs);
        double sum = 0.0;
        int count = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            analysis.push(x[i]);
            if (i > static_cast<size_t>(fs * 0.3) && i % 120 == 0) {
                const auto reading = analysis.pitchAt(analysis.now() - static_cast<int64_t>(fs * 0.05));
                if (reading.voiced) {
                    sum += centsBetween(fs / reading.period, 150.0);
                    ++count;
                }
            }
        }
        REQUIRE(count > 100);
        return sum / count;
    };
    CHECK(std::abs(meanCents(-48.0) - meanCents(6.0)) <= 2.0);
}

TEST_CASE("octave errors are rare") {
    const double fs = 48000.0;
    const auto x = loudPhrase(fs, 6.0, -3.0);
    const int syllable = static_cast<int>(fs * 0.21);
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    int voiced = 0, close = 0;
    const int lag = static_cast<int>(fs * 0.05);
    for (size_t i = 0; i < x.size(); ++i) {
        analysis.push(x[i]);
        const int64_t t = analysis.now() - lag;
        if (t <= 0 || analysis.now() % 120 != 0) continue;
        const int index = static_cast<int>(t / syllable);
        const double place = static_cast<double>(t % syllable) / syllable;
        // The middle of a vowel, not a consonant or the pause in every ninth syllable.
        if (index % 9 == 8 || place < 0.35 || place > 0.85) continue;
        const auto reading = analysis.pitchAt(t);
        if (reading.voiced) {
            ++voiced;
            const double hz = fs / reading.period;
            if (hz >= 125.0 * std::pow(2.0, -1.0 / 12.0) && hz <= 175.0 * std::pow(2.0, 1.0 / 12.0)) ++close;
        }
    }
    MESSAGE(voiced << " voiced readings, " << close << " close");
    REQUIRE(voiced > 500);
    CHECK(static_cast<double>(close) / voiced >= 0.95);
}

TEST_CASE("a creaky voice keeps its pitch") {
    // Every second glottal pulse is weaker, as in a low, relaxed voice. The sound repeats exactly only every two
    // periods, but what is heard is the pitch of the pulses.
    const double fs = 48000.0, f0 = 200.0;
    const int n = static_cast<int>(fs);
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    const int period = static_cast<int>(fs / f0), ring = static_cast<int>(fs * 0.004);
    for (int pulse = 0; pulse * period < n; ++pulse) {
        const float strength = pulse % 2 == 0 ? 0.3f : 0.15f;
        for (int k = 0; k < ring && pulse * period + k < n; ++k) {
            const double t = k / fs;
            x[static_cast<size_t>(pulse * period + k)] += static_cast<float>(strength * std::exp(-t / 0.001) * std::sin(2.0 * wers::kPi * 700.0 * t));
        }
    }
    const auto share = readAll(x, fs, 0.1, 50.0, [&](double) { return f0; });
    MESSAGE("voiced " << share.voicedShare() * 100.0 << "%, near the pulses " << share.closeShare() * 100.0 << "%");
    CHECK(share.voicedShare() >= 0.9);
    CHECK(share.closeShare() >= 0.95);
}

TEST_CASE("a lone reading does not decide") {
    const PitchReading right{ 320.0f, 0.9f, true }, wrong{ 640.0f, 0.6f, true }, none{};
    SUBCASE("a wrong octave among right ones") {
        const auto settled = settledReading({ right, right, wrong, right, right });
        CHECK(settled.voiced);
        CHECK(settled.period == doctest::Approx(320.0f));
    }
    SUBCASE("a gap in a voice") {
        const auto settled = settledReading({ right, right, none, right, right });
        CHECK(settled.voiced);
        CHECK(settled.period == doctest::Approx(320.0f));
    }
    SUBCASE("a lone voiced reading in silence") {
        CHECK_FALSE(settledReading({ none, none, right, none, none }).voiced);
    }
    SUBCASE("the middle of the periods that are voiced") {
        const auto settled = settledReading({ PitchReading{ 300.0f, 0.9f, true }, none, PitchReading{ 340.0f, 0.9f, true }, none, PitchReading{ 320.0f, 0.9f, true } });
        CHECK(settled.voiced);
        CHECK(settled.period == doctest::Approx(320.0f));
    }
}

TEST_CASE("marks follow the periods") {
    const double fs = 48000.0;
    SUBCASE("a steady voice") {
        const double period = fs / 150.0;
        const auto marks = collectMarks(vowel(150.0, -18.0, static_cast<int>(fs), fs), fs);
        REQUIRE(marks.size() > 100);
        double low = 1.0e9, high = -1.0e9;
        for (size_t i = 1; i < marks.size(); ++i) {
            const double distance = static_cast<double>(marks[i] - marks[i - 1]);
            CHECK(std::abs(distance - period) <= 0.03 * period);
            // Where each mark falls in its period, walking along from the first one.
            const double place = static_cast<double>(marks[i] - marks[0]) - period * std::round(static_cast<double>(marks[i] - marks[0]) / period);
            low = std::min(low, place);
            high = std::max(high, place);
        }
        CHECK(high - low <= 0.05 * period);
    }
    SUBCASE("a glide") {
        const double length = 2.0;
        const auto marks = collectMarks(glideVoice(110.0, 220.0, -18.0, static_cast<int>(fs * length), fs), fs);
        REQUIRE(marks.size() > 100);
        int close = 0;
        for (size_t i = 1; i < marks.size(); ++i) {
            const double period = fs / glideHz(110.0, 220.0, static_cast<double>(marks[i]) / fs, length);
            if (std::abs(static_cast<double>(marks[i] - marks[i - 1]) - period) <= 0.03 * period) ++close;
        }
        CHECK(static_cast<double>(close) / static_cast<double>(marks.size() - 1) >= 0.98);
    }
}

TEST_CASE("marks stop without a voice") {
    const double fs = 48000.0;
    const int half = static_cast<int>(fs * 0.5);
    auto x = vowel(150.0, -18.0, half, fs);
    append(x, noise(-18.0, half));
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    int64_t lastMark = 0;
    for (float v : x) {
        analysis.push(v);
        Mark mark;
        if (analysis.markNear(analysis.now(), analysis.now(), mark)) lastMark = std::max(lastMark, mark.position);
    }
    CHECK(lastMark > half - static_cast<int64_t>(fs * 0.05));
    CHECK(lastMark <= half + static_cast<int64_t>(fs * 0.05));
}

TEST_CASE("the recent input can be read back") {
    const double fs = 48000.0;
    const auto x = sine(440.0, -6.0, 4800, fs);
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    for (float v : x) analysis.push(v);
    const int64_t now = analysis.now();
    for (int back : { 2, 100, static_cast<int>(fs * 0.059) }) {
        CAPTURE(back);
        CHECK(analysis.sample(static_cast<double>(now - back)) == doctest::Approx(x[static_cast<size_t>(now - back)]));
        // Half way between two samples of a low tone.
        const double t = static_cast<double>(now - back) - 0.5;
        const float wanted = static_cast<float>(wers::dbToGainD(-6.0) * std::sin(2.0 * wers::kPi * 440.0 * t / fs));
        CHECK(analysis.sample(t) == doctest::Approx(wanted).epsilon(0.01));
    }
}

TEST_CASE("the level follows the voice") {
    const double fs = 48000.0;
    const int half = static_cast<int>(fs * 0.5);
    auto x = vowel(150.0, -18.0, half, fs);
    x.resize(static_cast<size_t>(half) * 2, 0.0f);
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    for (float v : x) {
        analysis.push(v);
        if (analysis.now() == half) {
            // Thirty milliseconds back, inside the sound: its own level.
            CHECK(wers::gainToDb(analysis.levelAt(analysis.now() - static_cast<int64_t>(fs * 0.03))) == doctest::Approx(-18.0).epsilon(0.1));
        }
    }
    CHECK(analysis.levelAt(analysis.now()) < wers::dbToGain(-100.0f));
}

TEST_CASE("a reset forgets the voice") {
    const double fs = 48000.0;
    const auto x = vowel(150.0, -18.0, static_cast<int>(fs * 0.5), fs);
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    for (float v : x) analysis.push(v);
    analysis.reset();
    CHECK(analysis.now() == 0);
    CHECK_FALSE(analysis.pitchAt(0).voiced);
    Mark mark;
    CHECK_FALSE(analysis.markNear(0, 0, mark));
}

TEST_CASE("analysis never allocates") {
    const double fs = 48000.0;
    const auto x = loudPhrase(fs, 2.0, -3.0);
    VoiceAnalysis analysis;
    analysis.prepare(fs);
    AllocationCounter counter;
    for (float v : x) {
        analysis.push(v);
        Mark mark;
        analysis.markNear(analysis.now(), analysis.now(), mark);
    }
    CHECK(counter.stop() == 0);
}
