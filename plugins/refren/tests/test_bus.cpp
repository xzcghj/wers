#include "doctest.h"
#include "Echo.h"
#include "Grain.h"
#include "Measure.h"
#include "MotionFx.h"
#include "RefrenEngine.h"
#include "Reverb.h"
#include "TestSignals.h"
#include "Tone.h"
#include <chrono>

using namespace refren;
using namespace wers::test;

namespace {

constexpr double kFs = 48000.0;
constexpr int kOversamplingLatency = 49;
constexpr std::array<Character, 4> kCharacters{ Character::Lamp, Character::Tape, Character::Transformer, Character::Transistor };

int latencyOf(double fs) { return static_cast<int>(std::lround(0.030 * fs)); }

struct Stereo {
    std::vector<float> left, right;
};

Stereo runEngine(const std::vector<float>& x, const RefrenParams& p, double fs = kFs, double bpm = 120.0) {
    RefrenEngine engine;
    engine.prepare(fs);
    engine.setTempo(bpm);
    engine.setParams(p);
    Stereo y{ x, x };
    for (size_t from = 0; from < x.size(); from += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, x.size() - from));
        float* channels[] = { y.left.data() + from, y.right.data() + from };
        engine.process(channels, 2, n);
    }
    return y;
}

Stereo runGrain(const std::vector<float>& x, float driveDb, Character character, double fs = kFs) {
    Grain grain;
    grain.prepare(fs);
    grain.set(driveDb, character);
    Stereo y{ x, x };
    for (size_t i = 0; i < x.size(); ++i) grain.tick(y.left[i], y.right[i]);
    return y;
}

Stereo runMotion(const std::vector<float>& x, Motion kind, float rate, float depth) {
    MotionFx motion;
    motion.prepare(kFs);
    motion.set(kind, rate, depth);
    Stereo y{ x, x };
    for (size_t i = 0; i < x.size(); ++i) motion.tick(y.left[i], y.right[i]);
    return y;
}

struct EchoSetting {
    EchoTime time = EchoTime::Eighth;
    float feedback = 0.25f;
    bool ping = false;
    float tone = 12000.0f;
    double bpm = 120.0;
};

Stereo runEcho(const std::vector<float>& x, const EchoSetting& s, double fs = kFs) {
    Echo echo;
    echo.prepare(fs);
    echo.set(s.time, s.feedback, s.ping, s.tone, s.bpm);
    Stereo y{ x, x };
    for (size_t i = 0; i < x.size(); ++i) echo.tick(x[i], x[i], y.left[i], y.right[i]);
    return y;
}

struct VerbSetting {
    float size = 0.5f, decay = 1.5f, tone = 16000.0f, pre = 0.0f, shimmer = 0.0f;
};

Stereo runVerb(const std::vector<float>& x, const VerbSetting& s, double fs = kFs) {
    Reverb reverb;
    reverb.prepare(fs);
    reverb.set(s.size, s.decay, s.tone, s.pre, s.shimmer);
    Stereo y{ x, x };
    for (size_t i = 0; i < x.size(); ++i) reverb.tick(x[i], x[i], y.left[i], y.right[i]);
    return y;
}

std::vector<float> impulse(int n, float height = 0.5f) {
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    x[0] = height;
    return x;
}

// The whole sound through a space, with nothing else going on.
RefrenParams spaceOnly() {
    RefrenParams p;
    for (auto& l : p.layer) l.on = false;
    p.sendDry = 1.0f;
    p.duck = 0.0f;
    p.delta = true;
    return p;
}

int firstAbove(const std::vector<float>& y, float level, int from = 0) {
    for (int i = from; i < static_cast<int>(y.size()); ++i)
        if (std::abs(y[static_cast<size_t>(i)]) > level) return i;
    return -1;
}

int peakPlace(const std::vector<float>& y, int from, int to) {
    int best = from;
    for (int i = from; i < to; ++i)
        if (std::abs(y[static_cast<size_t>(i)]) > std::abs(y[static_cast<size_t>(best)])) best = i;
    return best;
}

// RT60 from the backward integrated energy, from -5 to -35 dB.
double decaySeconds(const Stereo& y, double fs) {
    std::vector<double> tail(y.left.size());
    double sum = 0.0;
    for (size_t i = y.left.size(); i-- > 0;) {
        sum += static_cast<double>(y.left[i]) * y.left[i] + static_cast<double>(y.right[i]) * y.right[i];
        tail[i] = sum;
    }
    const double total = tail[0];
    int from = -1, to = -1;
    for (size_t i = 0; i < tail.size(); ++i) {
        const double db = 10.0 * std::log10(tail[i] / total + 1.0e-30);
        if (from < 0 && db <= -5.0) from = static_cast<int>(i);
        if (to < 0 && db <= -35.0) {
            to = static_cast<int>(i);
            break;
        }
    }
    REQUIRE(from >= 0);
    REQUIRE(to > from);
    return 2.0 * (to - from) / fs;
}

} // namespace

TEST_CASE("grain at zero changes nothing") {
    for (double fs : kRates) {
        CAPTURE(fs);
        const auto x = loudPhrase(fs, 0.5, -3.0);
        const auto y = runGrain(x, 0.0f, Character::Lamp, fs);
        CHECK(errorAgainstDelayDb(x, y.left, kOversamplingLatency) < -140.0);
        CHECK(errorAgainstDelayDb(x, y.right, kOversamplingLatency) < -140.0);
    }
}

TEST_CASE("grain keeps the level") {
    const auto x = loudPhrase(kFs, 6.0, -3.0);
    const int from = static_cast<int>(kFs * 2.0), to = static_cast<int>(x.size());
    const double input = weightedDb(x, from, to);
    for (Character character : kCharacters) {
        for (float drive : { 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 36.0f }) {
            CAPTURE(static_cast<int>(character)); CAPTURE(drive);
            const auto y = runGrain(x, drive, character);
            CHECK(std::abs(weightedDb(y.left, from + kOversamplingLatency, to) - input) <= 1.0);
        }
    }
}

TEST_CASE("tilt moves the balance") {
    const auto gainAt = [](float tilt, double hz) {
        RefrenParams p;
        p.consonants = 0.0f;
        p.delta = true;
        p.tilt = tilt;
        const auto x = sine(hz, -18.0, static_cast<int>(kFs * 0.5), kFs);
        const auto y = runEngine(x, p);
        p.tilt = 0.0f;
        const auto level = runEngine(x, p);
        return rmsDb(y.left, 12000) - rmsDb(level.left, 12000);
    };
    CHECK(std::abs(gainAt(1.0f, 15000.0) - 6.0) <= 1.0);
    CHECK(std::abs(gainAt(1.0f, 60.0) + 6.0) <= 1.0);
    CHECK(std::abs(gainAt(-1.0f, 15000.0) + 6.0) <= 1.0);
    CHECK(std::abs(gainAt(-1.0f, 60.0) - 6.0) <= 1.0);
    CHECK(std::abs(gainAt(1.0f, 1000.0)) <= 0.1);
    wers::Tilt flat;
    flat.prepare(kFs);
    flat.set(0.0f);
    const auto x = noise(-12.0, 4800);
    for (float v : x) REQUIRE(static_cast<float>(flat.tick(static_cast<double>(v))) == v);
}

TEST_CASE("autopan moves between the channels") {
    const auto y = runMotion(sine(500.0, -12.0, static_cast<int>(kFs * 4.0), kFs), Motion::Autopan, 1.0f, 1.0f);
    const int frame = static_cast<int>(kFs * 0.05);
    std::vector<double> left, right;
    double lowestPower = 1.0e9, highestPower = -1.0e9;
    for (int i = 0; i + frame <= static_cast<int>(y.left.size()); i += frame) {
        const double a = std::pow(10.0, rmsDb(y.left, i, i + frame) / 10.0), b = std::pow(10.0, rmsDb(y.right, i, i + frame) / 10.0);
        left.push_back(a);
        right.push_back(b);
        const double power = 10.0 * std::log10(a + b + 1.0e-20);
        lowestPower = std::min(lowestPower, power);
        highestPower = std::max(highestPower, power);
    }
    double meanA = 0.0, meanB = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        meanA += left[i] / static_cast<double>(left.size());
        meanB += right[i] / static_cast<double>(left.size());
    }
    double together = 0.0, ownA = 0.0, ownB = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        together += (left[i] - meanA) * (right[i] - meanB);
        ownA += (left[i] - meanA) * (left[i] - meanA);
        ownB += (right[i] - meanB) * (right[i] - meanB);
    }
    CHECK(together / std::sqrt(ownA * ownB) <= -0.9);
    CHECK(highestPower - lowestPower <= 2.0);
}

TEST_CASE("tremolo follows its depth") {
    const auto x = sine(500.0, -12.0, static_cast<int>(kFs * 2.0), kFs);
    const auto full = runMotion(x, Motion::Tremolo, 4.0f, 1.0f);
    const int frame = static_cast<int>(kFs * 0.01);
    double lowest = 0.0, highest = -300.0;
    for (int i = 0; i + frame <= static_cast<int>(x.size()); i += frame) {
        const double level = rmsDb(full.left, i, i + frame);
        lowest = std::min(lowest, level);
        highest = std::max(highest, level);
    }
    MESSAGE("from " << lowest << " to " << highest << " dB");
    CHECK(highest - lowest >= 20.0);
    const auto none = runMotion(x, Motion::Tremolo, 4.0f, 0.0f);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE(none.left[i] == x[i]);
}

TEST_CASE("flanger stays stable at its limit") {
    const auto y = runMotion(impulse(static_cast<int>(kFs * 3.0)), Motion::Flanger, 1.0f, 1.0f);
    for (float v : y.left) REQUIRE(std::isfinite(v));
    const double early = peakDb(y.left, 0, static_cast<int>(kFs * 0.1));
    const double late = peakDb(y.left, static_cast<int>(kFs * 2.0));
    MESSAGE("first " << early << " dB, after 2 s " << late << " dB");
    CHECK(late < early - 60.0);
    const auto x = sine(500.0, -12.0, static_cast<int>(kFs * 2.0), kFs);
    const auto none = runMotion(x, Motion::Flanger, 1.0f, 0.0f);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE(none.left[i] == x[i]);
}

TEST_CASE("echo lands on the beat") {
    const std::array<std::pair<EchoTime, double>, 6> times{ { { EchoTime::Sixteenth, 0.125 }, { EchoTime::Eighth, 0.25 }, { EchoTime::DottedEighth, 0.375 }, { EchoTime::Quarter, 0.5 }, { EchoTime::DottedQuarter, 0.75 }, { EchoTime::Half, 1.0 } } };
    const auto x = impulse(static_cast<int>(kFs * 1.5));
    for (const auto& item : times) {
        const double seconds = item.second;
        CAPTURE(seconds);
        EchoSetting s;
        s.time = item.first;
        s.feedback = 0.0f;
        const auto y = runEcho(x, s);
        CHECK(std::abs(peakPlace(y.left, 0, static_cast<int>(y.left.size())) - static_cast<int>(std::lround(seconds * kFs))) <= 1);
    }
    for (double bpm : { 60.0, 120.0, 180.0 }) {
        CAPTURE(bpm);
        EchoSetting s;
        s.time = EchoTime::Slap;
        s.feedback = 0.0f;
        s.bpm = bpm;
        const auto y = runEcho(x, s);
        CHECK(std::abs(peakPlace(y.left, 0, static_cast<int>(y.left.size())) - static_cast<int>(std::lround(0.08 * kFs))) <= 1);
    }
}

TEST_CASE("echo without tempo uses 120") {
    const auto x = impulse(static_cast<int>(kFs * 1.2));
    auto p = spaceOnly();
    p.echoMix = 1.0f;
    p.echoFeedback = 0.0f;
    p.echoToneHz = 12000.0f;
    p.duck = 0.0f;
    const int late = latencyOf(kFs);
    const auto placeAt = [&](double bpm) { const auto y = runEngine(x, p, kFs, bpm); return peakPlace(y.left, 0, static_cast<int>(y.left.size())) - late; };
    CHECK(std::abs(placeAt(0.0) - static_cast<int>(0.25 * kFs)) <= 1);
    CHECK(std::abs(placeAt(-5.0) - static_cast<int>(0.25 * kFs)) <= 1);
    CHECK(std::abs(placeAt(60.0) - static_cast<int>(0.5 * kFs)) <= 1);
}

TEST_CASE("tempo change does not click") {
    const int n = static_cast<int>(kFs * 3.0), change = static_cast<int>(kFs * 1.5);
    const auto run = [&](double later) {
        const auto x = sine(200.0, -12.0, n, kFs);
        Echo echo;
        echo.prepare(kFs);
        echo.set(EchoTime::Eighth, 0.3f, false, 8000.0f, 120.0);
        std::vector<float> y(x.size()), other(x.size());
        for (int i = 0; i < n; ++i) {
            if (i % 32 == 0) echo.set(EchoTime::Eighth, 0.3f, false, 8000.0f, i >= change ? later : 120.0);
            echo.tick(x[static_cast<size_t>(i)], x[static_cast<size_t>(i)], y[static_cast<size_t>(i)], other[static_cast<size_t>(i)]);
        }
        return worstJump(y, change, n);
    };
    const double steady = run(120.0), moved = run(90.0);
    MESSAGE("worst step: steady " << steady << ", with a change " << moved);
    CHECK(moved <= 2.0 * steady);
}

TEST_CASE("ping-pong alternates") {
    EchoSetting s;
    s.ping = true;
    s.feedback = 0.5f;
    const auto y = runEcho(impulse(static_cast<int>(kFs * 1.5)), s);
    const int beat = static_cast<int>(0.25 * kFs);
    const auto heardAt = [&](const std::vector<float>& z, int repeat) { return peakDb(z, repeat * beat - 50, repeat * beat + 50); };
    CHECK(heardAt(y.left, 1) > heardAt(y.right, 1) + 40.0);
    CHECK(heardAt(y.right, 2) > heardAt(y.left, 2) + 40.0);
    CHECK(heardAt(y.left, 3) > heardAt(y.right, 3) + 40.0);
}

TEST_CASE("echo fades with its feedback") {
    auto x = sine(200.0, -6.0, static_cast<int>(kFs * 0.05), kFs);
    x.resize(static_cast<size_t>(kFs * 3.0), 0.0f);
    EchoSetting s;
    s.feedback = 0.9f;
    const auto y = runEcho(x, s);
    for (float v : y.left) REQUIRE(std::isfinite(v));
    const int beat = static_cast<int>(0.25 * kFs), length = static_cast<int>(kFs * 0.05);
    for (int repeat = 1; repeat < 8; ++repeat) {
        CAPTURE(repeat);
        const double a = rmsDb(y.left, repeat * beat, repeat * beat + length), b = rmsDb(y.left, (repeat + 1) * beat, (repeat + 1) * beat + length);
        CHECK(std::abs(b - a + 0.92) <= 0.3);
    }
}

TEST_CASE("reverb decay follows the setting") {
    for (float decay : { 0.2f, 1.5f, 8.0f }) {
        CAPTURE(decay);
        VerbSetting s;
        s.decay = decay;
        auto x = bandNoise(200.0, 2000.0, -12.0, static_cast<int>(kFs * 0.01), kFs);
        x.resize(static_cast<size_t>(kFs * (decay * 1.5 + 0.5)), 0.0f);
        const auto y = runVerb(x, s);
        const double measured = decaySeconds(y, kFs);
        MESSAGE("set " << decay << " s, measured " << measured << " s");
        CHECK(std::abs(measured - decay) <= 0.25 * decay);
    }
}

TEST_CASE("reverb is dense") {
    const auto y = runVerb(impulse(static_cast<int>(kFs * 0.5)), VerbSetting{});
    const int ms = static_cast<int>(kFs * 0.001);
    const int from = static_cast<int>(kFs * 0.1), to = static_cast<int>(kFs * 0.2);
    const double mean = rmsDb(y.left, from, to);
    int filled = 0, total = 0;
    for (int i = from; i + ms <= to; i += ms) {
        ++total;
        if (rmsDb(y.left, i, i + ms) >= mean - 40.0) ++filled;
    }
    MESSAGE("holding sound: " << 100.0 * filled / total << "% of the milliseconds");
    CHECK(static_cast<double>(filled) / total >= 0.8);
}

TEST_CASE("reverb top follows its tone") {
    const auto topEnergy = [&](float tone) {
        VerbSetting s;
        s.tone = tone;
        s.decay = 2.0f;
        auto y = runVerb(impulse(static_cast<int>(kFs * 1.0)), s);
        wers::Biquad high;
        high.set(wers::Biquad::Type::HighPass, 4000.0, 0.7071, 0.0, kFs);
        double energy = 0.0;
        for (size_t i = 0; i < y.left.size(); ++i) {
            const double v = high.tick(static_cast<double>(y.left[i]));
            if (i > static_cast<size_t>(kFs * 0.1)) energy += v * v;
        }
        return 10.0 * std::log10(energy + 1.0e-30);
    };
    const double dark = topEnergy(1000.0), bright = topEnergy(16000.0);
    MESSAGE("above 4 kHz: tone 1 kHz " << dark << " dB, tone 16 kHz " << bright << " dB");
    CHECK(dark <= bright - 12.0);
}

TEST_CASE("predelay delays") {
    const auto start = [&](float pre) {
        VerbSetting s;
        s.pre = pre;
        const auto y = runVerb(impulse(static_cast<int>(kFs * 0.5)), s);
        return firstAbove(y.left, 1.0e-5f);
    };
    const int none = start(0.0f);
    REQUIRE(none > 0);
    CHECK(std::abs((start(40.0f) - none) / kFs * 1000.0 - 40.0) <= 2.0);
    CHECK(std::abs((start(80.0f) - none) / kFs * 1000.0 - 80.0) <= 2.0);
}

TEST_CASE("shimmer adds the octave") {
    auto x = sine(500.0, -12.0, static_cast<int>(kFs), kFs);
    x.resize(static_cast<size_t>(kFs * 2.0), 0.0f);
    // Around the note, because two pieces of 50 ms that are out of step on this very tone put it 20 Hz to each side.
    const auto aroundDb = [](const std::vector<float>& tail, double hz) {
        double power = 0.0;
        for (double f = hz - 60.0; f <= hz + 60.0; f += 20.0) power += std::pow(10.0, toneLevelDb(tail, f, kFs) * 0.1);
        return 10.0 * std::log10(power);
    };
    const auto tailLevels = [&](float shimmer, double& low, double& high) {
        VerbSetting s;
        s.decay = 3.0f;
        s.shimmer = shimmer;
        const auto y = runVerb(x, s);
        const std::vector<float> tail(y.left.begin() + static_cast<int>(kFs * 1.25), y.left.end());
        low = aroundDb(tail, 500.0);
        high = aroundDb(tail, 1000.0);
    };
    double low = 0.0, high = 0.0;
    tailLevels(1.0f, low, high);
    MESSAGE("shimmer 100%: 500 Hz " << low << " dB, 1 kHz " << high << " dB");
    CHECK(high >= low - 12.0);
    tailLevels(0.0f, low, high);
    MESSAGE("shimmer 0%: 500 Hz " << low << " dB, 1 kHz " << high << " dB");
    CHECK(high <= low - 40.0);
}

TEST_CASE("feedback at the limits stays stable") {
    auto p = spaceOnly();
    p.echoMix = 1.0f;
    p.echoFeedback = 0.9f;
    p.verbMix = 1.0f;
    p.verbDecay = 8.0f;
    p.verbShimmer = 1.0f;
    p.motion = Motion::Flanger;
    p.motionDepth = 1.0f;
    p.motionRate = 1.0f;
    p.layer[0].on = true;
    p.consonants = 0.0f;
    auto x = loudPhrase(kFs, 1.0, -3.0);
    x.resize(static_cast<size_t>(kFs * 60.0), 0.0f);
    const auto y = runEngine(x, p);
    for (float v : y.left) REQUIRE(std::isfinite(v));
    double previous = 0.0;
    for (int part = 0; part < 6; ++part) {
        const double level = rmsDb(y.left, static_cast<int>(kFs * (part * 10.0 + 2.0)), static_cast<int>(kFs * (part * 10.0 + 10.0)));
        MESSAGE("from " << part * 10 + 2 << " s: " << level << " dB");
        if (part > 0) CHECK(level < previous);
        previous = level;
    }
}

TEST_CASE("duck clears the way for words") {
    const int voiced = static_cast<int>(kFs * 3.0);
    auto x = loudPhrase(kFs, 3.0, -6.0);
    x.resize(static_cast<size_t>(kFs * 6.0), 0.0f);
    auto p = spaceOnly();
    p.verbMix = 1.0f;
    p.verbDecay = 3.0f;
    p.duck = 0.0f;
    const auto open = runEngine(x, p);
    p.duck = 1.0f;
    const auto ducked = runEngine(x, p);
    const int from = static_cast<int>(kFs);
    const double during = rmsDb(open.left, from, voiced) - rmsDb(ducked.left, from, voiced);
    const int later = voiced + static_cast<int>(kFs);
    const double after = rmsDb(open.left, later, later + static_cast<int>(kFs)) - rmsDb(ducked.left, later, later + static_cast<int>(kFs));
    MESSAGE("under the words down " << during << " dB, a second after them " << after << " dB");
    CHECK(during >= 6.0);
    CHECK(std::abs(after) <= 1.0);
}

TEST_CASE("dry send feeds the space") {
    const auto x = loudPhrase(kFs, 1.0, -6.0);
    auto p = spaceOnly();
    p.verbMix = 1.0f;
    const auto sent = runEngine(x, p);
    CHECK(rmsDb(sent.left) > -40.0);
    p.sendDry = 0.0f;
    const auto notSent = runEngine(x, p);
    CHECK(rmsDb(notSent.left) < -120.0);
}

TEST_CASE("the output follows the formula") {
    const auto x = sine(300.0, -18.0, static_cast<int>(kFs), kFs);
    const int late = latencyOf(kFs);
    RefrenParams p;
    p.consonants = 0.0f;
    p.layer[0].voices = 1;
    p.layer[0].timeMs = 0.0f;
    p.layer[0].detuneCents = 0.0f;
    p.layer[0].width = 0.0f;
    p.layer[0].levelDb = -3.0f;
    auto wetOnly = p;
    wetOnly.delta = true;
    const auto wet = runEngine(x, wetOnly);
    p.amount = 0.5f;
    p.dryDb = -12.0f;
    p.trimDb = 6.0f;
    p.outputDb = -6.0f;
    const auto y = runEngine(x, p);
    const double a = 0.5, dry = wers::dbToGainD(-12.0), trim = wers::dbToGainD(6.0), output = wers::dbToGainD(-6.0);
    double error = 0.0, signal = 0.0;
    for (size_t i = static_cast<size_t>(kFs * 0.2); i < x.size(); ++i) {
        const double wanted = ((1.0 + a * (dry - 1.0)) * x[i - static_cast<size_t>(late)] + a * wet.left[i]) * (1.0 + a * (trim - 1.0)) * output;
        const double d = static_cast<double>(y.left[i]) - wanted;
        error += d * d;
        signal += wanted * wanted;
    }
    const double ratio = 10.0 * std::log10(error / signal);
    MESSAGE("error " << ratio << " dB");
    CHECK(ratio < -44.0); // the same as 0.05 dB
}

TEST_CASE("delta is the wet signal") {
    const auto x = loudPhrase(kFs, 1.0, -6.0);
    RefrenParams p;
    p.layer[0].voices = 3;
    p.verbMix = 0.3f;
    const auto all = runEngine(x, p);
    p.delta = true;
    const auto wet = runEngine(x, p);
    const int late = latencyOf(kFs);
    double worst = 0.0;
    for (size_t i = static_cast<size_t>(late); i < x.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(wet.left[i]) + x[i - static_cast<size_t>(late)] - all.left[i]));
    CHECK(20.0 * std::log10(worst + 1.0e-20) < -100.0);
}

TEST_CASE("grain and motion work on the wet sound") {
    const auto x = sine(200.0, -6.0, static_cast<int>(kFs), kFs);
    RefrenParams p;
    p.consonants = 0.0f;
    p.layer[0].voices = 1;
    p.layer[0].detuneCents = 0.0f;
    p.layer[0].timeMs = 0.0f;
    p.delta = true;
    const auto thirdOf = [&](const RefrenParams& q) {
        const auto y = runEngine(x, q);
        const std::vector<float> part(y.left.begin() + 24000, y.left.end());
        return toneLevelDb(part, 600.0, kFs) - toneLevelDb(part, 200.0, kFs);
    };
    const double clean = thirdOf(p);
    p.grainDb = 24.0f;
    p.grainChar = Character::Transistor;
    const double driven = thirdOf(p);
    MESSAGE("third harmonic: clean " << clean << " dB, driven " << driven << " dB");
    CHECK(clean < -60.0);
    CHECK(driven > clean + 20.0);
    p.grainDb = 0.0f;
    p.motion = Motion::Tremolo;
    p.motionRate = 4.0f;
    p.motionDepth = 1.0f;
    const auto y = runEngine(x, p);
    const int frame = static_cast<int>(kFs * 0.01);
    double lowest = 0.0, highest = -300.0;
    for (int i = 4800; i + frame <= static_cast<int>(y.left.size()); i += frame) {
        const double level = rmsDb(y.left, i, i + frame);
        lowest = std::min(lowest, level);
        highest = std::max(highest, level);
    }
    CHECK(highest - lowest >= 20.0);
}

TEST_CASE("same result in any block size with everything on") {
    RefrenParams p;
    p.grainDb = 12.0f;
    p.tilt = 0.3f;
    p.motion = Motion::Flanger;
    p.motionDepth = 0.5f;
    p.echoMix = 0.3f;
    p.echoPing = true;
    p.verbMix = 0.3f;
    p.verbShimmer = 0.5f;
    p.sendDry = 0.2f;
    p.layer[1].on = true;
    p.layer[1].semis = 12;
    CHECK(sameInAnyBlockSize({ loudPhrase(kFs, 3.0, -3.0, 5u), loudPhrase(kFs, 3.0, -3.0, 9u) }, [&] {
        auto engine = std::make_shared<RefrenEngine>();
        engine->prepare(kFs);
        engine->setParams(p);
        return [engine](float* const* channels, int count, int n) { engine->process(channels, count, n); };
    }));
}

TEST_CASE("a long silence costs nothing") {
    RefrenParams p;
    p.grainDb = 12.0f;
    p.motion = Motion::Flanger;
    p.motionDepth = 0.5f;
    p.echoMix = 0.5f;
    p.echoFeedback = 0.5f;
    p.verbMix = 0.5f;
    p.verbDecay = 1.5f;
    p.layer[1].on = true;
    p.layer[1].semis = 12;
    p.layer[2].on = true;
    p.layer[2].source = Source::Whisper;
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setParams(p);
    auto voice = loudPhrase(kFs, 2.0, -6.0);
    std::vector<float> silence(static_cast<size_t>(kFs * 60.0), 0.0f);
    const auto secondsFor = [&](std::vector<float>& x, size_t from, size_t count) {
        const auto start = std::chrono::steady_clock::now();
        for (size_t i = from; i < from + count; i += 256) {
            float* channels[] = { x.data() + i };
            engine.process(channels, 1, static_cast<int>(std::min<size_t>(256, from + count - i)));
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const double sounding = secondsFor(voice, 0, voice.size());
    secondsFor(silence, 0, static_cast<size_t>(kFs * 54.0));
    // The quickest of a few, so one busy moment of the computer does not count.
    double quiet = 1.0e9;
    for (size_t part = 0; part < 3; ++part) quiet = std::min(quiet, secondsFor(silence, static_cast<size_t>(kFs * 54.0) + part * voice.size(), voice.size()));
    MESSAGE(sounding << " s with sound, " << quiet << " s with silence");
    CHECK(quiet <= 1.5 * sounding);
    CHECK(peakDb(silence, static_cast<int>(kFs * 58.0), static_cast<int>(kFs * 60.0)) < -140.0);
}

TEST_CASE("reset forgets the voice") {
    RefrenParams p;
    p.grainDb = 12.0f;
    p.echoMix = 0.5f;
    p.verbMix = 0.5f;
    p.verbShimmer = 0.5f;
    p.motion = Motion::Autopan;
    p.motionDepth = 1.0f;
    p.layer[1].on = true;
    p.layer[1].semis = -12;
    p.layer[2].on = true;
    p.layer[2].source = Source::Sub;
    const auto x = loudPhrase(kFs, 1.0, -6.0);
    const auto fresh = runEngine(x, p);
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setTempo(120.0);
    engine.setParams(p);
    Stereo y{ x, x };
    float* first[] = { y.left.data(), y.right.data() };
    engine.process(first, 2, static_cast<int>(x.size()));
    engine.reset();
    y = Stereo{ x, x };
    float* second[] = { y.left.data(), y.right.data() };
    engine.process(second, 2, static_cast<int>(x.size()));
    CHECK(std::memcmp(y.left.data(), fresh.left.data(), x.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(y.right.data(), fresh.right.data(), x.size() * sizeof(float)) == 0);
}

TEST_CASE("tails stop on a change of preset") {
    auto p = spaceOnly();
    p.verbMix = 1.0f;
    p.verbDecay = 8.0f;
    p.echoMix = 1.0f;
    p.echoFeedback = 0.9f;
    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setParams(p);
    auto voice = loudPhrase(kFs, 1.0, -6.0);
    voice.resize(static_cast<size_t>(kFs * 1.1), 0.0f); // so what is left is only the tail, and nothing still on its way
    float* now[] = { voice.data() };
    engine.process(now, 1, static_cast<int>(voice.size()));
    REQUIRE(peakDb(voice, static_cast<int>(kFs * 1.05)) > -60.0);
    p.verbSize = 0.9f; // a setting that cannot be smoothed
    engine.setParams(p);
    std::vector<float> after(static_cast<size_t>(kFs * 0.5), 0.0f);
    float* later[] = { after.data() };
    engine.process(later, 1, static_cast<int>(after.size()));
    const double tail = peakDb(after, static_cast<int>(kFs * 0.1));
    MESSAGE("100 ms after the change: " << tail << " dB");
    CHECK(tail < -120.0);
}

TEST_CASE("bus changes that cannot be smoothed do not click") {
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
    RefrenParams full;
    full.grainDb = 12.0f;
    full.motionDepth = 0.5f;
    full.echoMix = 0.3f;
    full.verbMix = 0.3f;
    // Without the dry voice, and then with only the spaces, a click has nothing to hide behind.
    RefrenParams wetAlone = full;
    wetAlone.delta = true;
    RefrenParams spacesAlone = wetAlone;
    spacesAlone.layer[0].on = false;
    spacesAlone.sendDry = 1.0f;
    spacesAlone.duck = 0.0f;
    spacesAlone.echoMix = 1.0f;
    spacesAlone.verbMix = 1.0f;
    for (const RefrenParams& base : { full, wetAlone, spacesAlone }) {
        std::vector<std::pair<const char*, RefrenParams>> changes;
        { auto p = base; p.grainChar = Character::Transistor; changes.emplace_back("character", p); }
        { auto p = base; p.motion = Motion::Flanger; changes.emplace_back("motion", p); }
        { auto p = base; p.echoTime = EchoTime::Quarter; changes.emplace_back("echo time", p); }
        { auto p = base; p.echoPing = true; changes.emplace_back("ping-pong", p); }
        { auto p = base; p.verbSize = 0.9f; changes.emplace_back("size", p); }
        for (const auto& x : { sine(150.0, -12.0, n, kFs), loudPhrase(kFs, 2.0, -6.0) }) {
            const double steady = worstJump(run(x, base, base), change, n);
            for (const auto& item : changes) {
                const std::string what = item.first;
                CAPTURE(what);
                CAPTURE(base.delta);
                CAPTURE(base.layer[0].on);
                const auto y = run(x, base, item.second);
                for (float v : y) REQUIRE(std::isfinite(v));
                const double other = worstJump(run(x, item.second, item.second), change, n);
                CHECK(worstJump(y, change, n) <= 2.0 * std::max(steady, other));
            }
        }
    }
}
