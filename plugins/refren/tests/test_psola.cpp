#include "doctest.h"
#include "AllocationCount.h"
#include "Measure.h"
#include "Psola.h"
#include "TestSignals.h"
#include "VoiceSignals.h"

using namespace refren;
using namespace refren::test;
using namespace wers::test;

namespace {

int streamDelay(double fs) { return static_cast<int>(std::lround(0.030 * fs)) - 49 - 2; }

std::vector<float> runPsola(const std::vector<float>& x, double fs, const PsolaSettings& settings) {
    VoiceAnalysis voice;
    voice.prepare(fs);
    Psola psola;
    psola.prepare(fs, streamDelay(fs));
    psola.set(settings);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        voice.push(x[i]);
        y[i] = psola.tick(voice);
    }
    return y;
}

PsolaSettings shifted(float semitones, float formant = 0.0f) {
    PsolaSettings s;
    s.semitones = semitones;
    s.formantSemitones = formant;
    return s;
}

// The share of frames on the wanted pitch. wantedHz gets the moment of the input that a frame plays.
template <typename Wanted>
double shareOnPitch(const std::vector<float>& y, double fs, double fromSeconds, double cents, Wanted&& wantedHz) {
    const int delay = streamDelay(fs);
    const int step = static_cast<int>(fs * 0.025);
    int total = 0, close = 0;
    for (int start = static_cast<int>(fs * fromSeconds);; start += step) {
        const double wantedAtStart = wantedHz(static_cast<double>(start - delay) / fs);
        const int length = static_cast<int>(3.0 * fs / wantedAtStart);
        if (start + length + static_cast<int>(1.2 * fs / wantedAtStart) + 2 >= static_cast<int>(y.size())) break;
        const double wanted = wantedHz((start + 0.5 * length - delay) / fs);
        const double hz = framePitchHz(y, start, length, fs, wanted / 1.15, wanted * 1.15);
        ++total;
        if (hz > 0.0 && std::abs(centsBetween(hz, wanted)) <= cents) ++close;
    }
    REQUIRE(total > 5);
    return static_cast<double>(close) / total;
}

// A voiced sound with one formant and nothing else that pulls on a measurement of it.
std::vector<float> oneFormant(double f0, double formantHz, double db, int n, double fs) {
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    for (int h = 1; h * f0 < 6000.0; ++h) {
        const double amp = 0.02 + 1.0 / (1.0 + std::pow((h * f0 - formantHz) / 150.0, 2.0));
        for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] += static_cast<float>(amp * std::sin(2.0 * wers::kPi * h * f0 * i / fs + h * 0.7));
    }
    double energy = 0.0;
    for (float v : x) energy += static_cast<double>(v) * v;
    const double gain = wers::dbToGainD(db) / std::sqrt(energy / n + 1e-20);
    for (auto& v : x) v = static_cast<float>(v * gain);
    return x;
}

// Where the highest harmonic within the range stands, from the levels of the harmonics around it.
double formantPeakHz(const std::vector<float>& y, double f0, double fs, double lowHz, double highHz) {
    int best = 0;
    double bestDb = -400.0;
    std::vector<double> levels(static_cast<size_t>(highHz / f0) + 3, -400.0);
    for (int h = 1; h * f0 < highHz + 2.0 * f0; ++h) {
        levels[static_cast<size_t>(h)] = toneLevelDb(y, h * f0, fs);
        if (h * f0 >= lowHz && h * f0 <= highHz && levels[static_cast<size_t>(h)] > bestDb) {
            bestDb = levels[static_cast<size_t>(h)];
            best = h;
        }
    }
    REQUIRE(best > 1);
    const double left = levels[static_cast<size_t>(best - 1)], right = levels[static_cast<size_t>(best + 1)];
    const double curve = left - 2.0 * bestDb + right;
    return (best + (curve < 0.0 ? 0.5 * (left - right) / curve : 0.0)) * f0;
}

} // namespace

TEST_CASE("octave down and up land on pitch") {
    for (double fs : { 44100.0, 48000.0, 96000.0 }) {
        for (float semitones : { -12.0f, 12.0f }) {
            const double ratio = std::pow(2.0, semitones / 12.0);
            for (double f0 : { 90.0, 150.0, 250.0, 400.0 }) {
                CAPTURE(fs); CAPTURE(semitones); CAPTURE(f0);
                const auto y = runPsola(vowel(f0, -18.0, static_cast<int>(fs), fs), fs, shifted(semitones));
                CHECK(shareOnPitch(y, fs, 0.2, 5.0, [&](double) { return f0 * ratio; }) >= 0.9);
            }
            const double length = 2.0;
            for (const auto& glide : std::array<std::pair<double, double>, 2>{ { { 110.0, 220.0 }, { 300.0, 150.0 } } }) {
                const double from = glide.first, to = glide.second;
                CAPTURE(fs); CAPTURE(semitones); CAPTURE(from);
                const auto y = runPsola(glideVoice(from, to, -18.0, static_cast<int>(fs * length), fs), fs, shifted(semitones));
                const double share = shareOnPitch(y, fs, 0.2, 5.0, [&](double t) { return glideHz(from, to, t, length) * ratio; });
                MESSAGE("glide from " << from << " by " << semitones << " at " << fs << ": " << share * 100.0 << "%");
                CHECK(share >= 0.9);
            }
        }
    }
}

TEST_CASE("other intervals land on pitch") {
    const double fs = 48000.0;
    const auto x = vowel(150.0, -18.0, static_cast<int>(fs), fs);
    for (float semitones : { -24.0f, -7.0f, -5.0f, 5.0f, 7.0f, 24.0f }) {
        CAPTURE(semitones);
        const auto y = runPsola(x, fs, shifted(semitones));
        CHECK(shareOnPitch(y, fs, 0.2, 5.0, [&](double) { return 150.0 * std::pow(2.0, semitones / 12.0); }) >= 0.9);
    }
}

TEST_CASE("robot holds the note") {
    const double fs = 48000.0;
    const auto x = glideVoice(110.0, 180.0, -18.0, static_cast<int>(fs * 2.0), fs);
    for (float note : { 130.81f, 220.0f }) {
        CAPTURE(note);
        PsolaSettings s;
        s.fixedNote = true;
        s.noteHz = note;
        const auto y = runPsola(x, fs, s);
        CHECK(shareOnPitch(y, fs, 0.2, 5.0, [&](double) { return static_cast<double>(note); }) >= 0.9);
    }
}

TEST_CASE("formants move and pitch stays") {
    const double fs = 48000.0, f0 = 110.0;
    const auto x = oneFormant(f0, 1200.0, -18.0, static_cast<int>(fs * 2.0), fs);
    const auto middle = [&](const std::vector<float>& y) { return std::vector<float>(y.begin() + static_cast<int>(fs * 0.5), y.end()); };
    const double plain = formantPeakHz(middle(runPsola(x, fs, shifted(0.0f))), f0, fs, 1000.0, 1450.0);
    MESSAGE("no shift: formant at " << plain << " Hz");
    for (float formant : { -6.0f, 6.0f }) {
        CAPTURE(formant);
        const double ratio = std::pow(2.0, formant / 12.0);
        const auto y = runPsola(x, fs, shifted(0.0f, formant));
        const double peak = formantPeakHz(middle(y), f0, fs, 1200.0 * ratio / 1.2, 1200.0 * ratio * 1.2);
        MESSAGE("shift " << formant << ": formant at " << peak << " Hz");
        CHECK(std::abs(centsBetween(peak / plain, ratio)) <= 100.0);
        CHECK(shareOnPitch(y, fs, 0.2, 5.0, [&](double) { return f0; }) >= 0.9);
    }
}

TEST_CASE("level stays") {
    const double fs = 48000.0;
    const auto x = vowel(150.0, -18.0, static_cast<int>(fs), fs);
    const int from = static_cast<int>(fs * 0.2);
    const double input = rmsDb(x, from);
    for (const auto& settings : { shifted(-12.0f), shifted(12.0f), shifted(0.0f, -6.0f), shifted(0.0f, 6.0f), shifted(0.0f) }) {
        CAPTURE(settings.semitones); CAPTURE(settings.formantSemitones);
        const double change = rmsDb(runPsola(x, fs, settings), from) - input;
        MESSAGE("interval " << settings.semitones << ", formant " << settings.formantSemitones << ": " << change << " dB");
        CHECK(std::abs(change) <= 1.5);
    }
}

TEST_CASE("stream stays in time") {
    const double fs = 48000.0;
    const auto s = makeSyllables(fs, 8);
    const int delay = streamDelay(fs);
    std::vector<float> reference(s.signal.size(), 0.0f);
    std::copy(s.signal.begin(), s.signal.end() - delay, reference.begin() + delay);
    for (float semitones : { 12.0f, -12.0f, 7.0f }) {
        CAPTURE(semitones);
        const double lag = envelopeLagMs(reference, runPsola(s.signal, fs, shifted(semitones)), fs);
        MESSAGE("interval " << semitones << ": " << lag << " ms");
        CHECK(std::abs(lag) <= 2.0);
    }
}

TEST_CASE("the stream starts and stops with the voice") {
    const double fs = 48000.0;
    const auto s = makeSyllables(fs, 8);
    const int delay = streamDelay(fs), window = static_cast<int>(fs * 0.004);
    std::vector<float> reference(s.signal.size(), 0.0f);
    std::copy(s.signal.begin(), s.signal.end() - delay, reference.begin() + delay);
    // Level around each sample, so the edges of different sounds can be compared.
    const auto levels = [&](const std::vector<float>& x) {
        std::vector<double> power(x.size(), 0.0);
        double sum = 0.0;
        for (size_t i = 0; i < x.size(); ++i) {
            sum += static_cast<double>(x[i]) * x[i];
            if (i >= static_cast<size_t>(window)) sum -= static_cast<double>(x[i - static_cast<size_t>(window)]) * x[i - static_cast<size_t>(window)];
            if (i >= static_cast<size_t>(window / 2)) power[i - static_cast<size_t>(window / 2)] = sum;
        }
        return power;
    };
    const auto edges = [&](const std::vector<double>& power, int vowel, double& rise, double& fall) {
        const auto [from, to] = s.vowels[static_cast<size_t>(vowel)];
        double full = 0.0;
        for (int i = from + delay + static_cast<int>(fs * 0.15); i < to + delay - static_cast<int>(fs * 0.1); ++i) full += power[static_cast<size_t>(i)];
        full /= to - from - static_cast<int>(fs * 0.25);
        int i = from + delay - static_cast<int>(fs * 0.04);
        while (power[static_cast<size_t>(i)] < 0.25 * full) ++i;
        rise = i / fs * 1000.0;
        i = to + delay + static_cast<int>(fs * 0.04);
        while (power[static_cast<size_t>(i)] < 0.25 * full) --i;
        fall = i / fs * 1000.0;
    };
    const auto wanted = levels(reference);
    for (float semitones : { 12.0f, -12.0f, 7.0f, -5.0f }) {
        CAPTURE(semitones);
        const auto got = levels(runPsola(s.signal, fs, shifted(semitones)));
        double rise = 0.0, fall = 0.0;
        for (int vowel = 1; vowel < 8; ++vowel) {
            double wantedRise, wantedFall, gotRise, gotFall;
            edges(wanted, vowel, wantedRise, wantedFall);
            edges(got, vowel, gotRise, gotFall);
            rise += (gotRise - wantedRise) / 7.0;
            fall += (gotFall - wantedFall) / 7.0;
        }
        MESSAGE("interval " << semitones << ": starts " << rise << " ms, stops " << fall << " ms against the voice");
        CHECK(std::abs(rise) <= 4.0);
        // The pieces of a lowered voice stand far apart, and the last one cannot be closer than half that distance.
        const double distanceMs = 1000.0 / 150.0 / std::pow(2.0, semitones / 12.0);
        CHECK(std::abs(fall) <= std::max(4.0, 0.5 * distanceMs));
    }
}

TEST_CASE("unvoiced goes silent") {
    const double fs = 48000.0;
    const int half = static_cast<int>(fs * 0.5);
    auto x = vowel(150.0, -18.0, half, fs);
    append(x, noise(-18.0, half));
    const auto y = runPsola(x, fs, shifted(12.0f));
    const int delay = streamDelay(fs);
    const double during = rmsDb(y, static_cast<int>(fs * 0.2), half);
    const double after = rmsDb(y, half + delay + static_cast<int>(fs * 0.04));
    MESSAGE("voice " << during << " dB, then " << after << " dB");
    CHECK(during - after >= 40.0);
}

TEST_CASE("fast voiced flips do not click") {
    const double fs = 48000.0;
    const int part = static_cast<int>(fs * 0.06);
    std::vector<float> x;
    for (int i = 0; i < 25; ++i) {
        append(x, vowel(120.0 + 10.0 * (i % 5), -18.0, part, fs));
        append(x, bandNoise(4000.0, 9000.0, -26.0, part, fs, static_cast<uint32_t>(i + 1)));
    }
    const int count = static_cast<int>(x.size());
    for (float semitones : { 12.0f, -12.0f }) {
        CAPTURE(semitones);
        const auto y = runPsola(x, fs, shifted(semitones));
        for (float v : y) REQUIRE(std::isfinite(v));
        const double input = worstJump(x, 0, count), output = worstJump(y, 0, count);
        MESSAGE("worst step: input " << input << ", output " << output);
        CHECK(output <= 2.0 * input);
    }
}

TEST_CASE("an octave error does not click") {
    const double fs = 48000.0;
    const int half = static_cast<int>(fs * 0.4);
    auto x = vowel(100.0, -18.0, half, fs);
    append(x, vowel(200.0, -18.0, half, fs));
    append(x, vowel(100.0, -18.0, half, fs));
    const int count = static_cast<int>(x.size());
    for (float semitones : { 12.0f, -12.0f, 0.0f }) {
        CAPTURE(semitones);
        const auto y = runPsola(x, fs, shifted(semitones, semitones == 0.0f ? 4.0f : 0.0f));
        for (float v : y) REQUIRE(std::isfinite(v));
        CHECK(worstJump(y, 0, count) <= 2.0 * worstJump(x, 0, count));
    }
}

TEST_CASE("a chord stays under control") {
    const double fs = 48000.0;
    const int n = static_cast<int>(fs * 2.0);
    auto x = vowel(110.0, -18.0, n, fs);
    const auto b = vowel(165.0, -18.0, n, fs), c = vowel(220.0, -18.0, n, fs);
    for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] += b[static_cast<size_t>(i)] + c[static_cast<size_t>(i)];
    // Hits like those of a drum, four of them a second.
    for (int i = 0; i < n; i += static_cast<int>(fs * 0.25))
        for (int k = 0; k < 400 && i + k < n; ++k) x[static_cast<size_t>(i + k)] += 0.5f * static_cast<float>(std::exp(-k / 80.0) * std::sin(k * 0.03));
    const double input = peakDb(x);
    for (float semitones : { 12.0f, -12.0f, 7.0f }) {
        CAPTURE(semitones);
        const auto y = runPsola(x, fs, shifted(semitones));
        for (float v : y) REQUIRE(std::isfinite(v));
        const double peak = peakDb(y);
        MESSAGE("input " << input << " dB, output " << peak << " dB");
        CHECK(peak <= input + 6.0);
    }
}

TEST_CASE("a reset starts the stream again") {
    const double fs = 48000.0;
    const auto x = vowel(150.0, -18.0, static_cast<int>(fs * 0.5), fs);
    VoiceAnalysis voice;
    voice.prepare(fs);
    Psola psola;
    psola.prepare(fs, streamDelay(fs));
    psola.set(shifted(12.0f));
    for (float v : x) {
        voice.push(v);
        psola.tick(voice);
    }
    voice.reset();
    psola.reset();
    for (float v : x) {
        voice.push(v);
        psola.tick(voice);
    }
    const auto fresh = runPsola(x, fs, shifted(12.0f));
    voice.reset();
    psola.reset();
    for (size_t i = 0; i < x.size(); ++i) {
        voice.push(x[i]);
        REQUIRE(psola.tick(voice) == fresh[i]);
    }
}

TEST_CASE("psola never allocates") {
    const double fs = 48000.0;
    const auto x = loudPhrase(fs, 2.0, -3.0);
    VoiceAnalysis voice;
    voice.prepare(fs);
    Psola psola;
    psola.prepare(fs, streamDelay(fs));
    AllocationCounter counter;
    float semitones = -12.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        if (i % 4800 == 0) {
            PsolaSettings s;
            s.semitones = semitones;
            s.formantSemitones = semitones * 0.5f;
            psola.set(s);
            semitones += 3.0f;
        }
        voice.push(x[i]);
        psola.tick(voice);
    }
    CHECK(counter.stop() == 0);
}
