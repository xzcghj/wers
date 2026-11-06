#include "MeasureVoice.h"
#include "ListedAudio.h"
#include "Measure.h"
#include "RefrenEngine.h"
#include "TestSignals.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace refren {

namespace {

constexpr double kHopSeconds = 0.01, kReadBackSeconds = 0.05, kFrameSeconds = 0.02, kSettleSeconds = 1.0;
constexpr double kLowHz = 1000.0, kHighHz = 4000.0, kSibilantOverLowDb = 6.0, kSibilantRangeDb = 40.0;
constexpr float kLoudLevel = 0.01f;

double fromDb(double db) { return std::pow(10.0, db * 0.1); }
double toDb(double power) { return 10.0 * std::log10(power + 1.0e-30); }

struct Stereo {
    std::vector<float> left, right;
};

Stereo run(const std::vector<float>& x, const RefrenParams& p, double fs) {
    RefrenEngine engine;
    engine.prepare(fs);
    engine.setParams(p);
    Stereo y{ x, x };
    for (size_t from = 0; from < x.size(); from += 1024) {
        float* channels[] = { y.left.data() + from, y.right.data() + from };
        engine.process(channels, 2, static_cast<int>(std::min<size_t>(1024, x.size() - from)));
    }
    return y;
}

// The voiced share and the steps of the pitch, from what the analysis reads.
void measurePitch(const std::vector<float>& mono, double fs, SongNumbers& result) {
    VoiceAnalysis voice;
    voice.prepare(fs);
    const int64_t hop = std::max<int64_t>(1, std::llround(fs * kHopSeconds)), back = std::llround(fs * kReadBackSeconds);
    int loud = 0, loudVoiced = 0;
    float lastPeriod = 0.0f;
    bool lastVoiced = false;
    std::vector<double> steps;
    for (const float v : mono) {
        voice.push(v);
        const int64_t now = voice.now();
        if (now % hop != 0 || now < back) continue;
        const auto reading = voice.pitchAt(now - back);
        if (voice.levelAt(now - back) > kLoudLevel) {
            ++loud;
            if (reading.voiced) ++loudVoiced;
        }
        if (reading.voiced && lastVoiced) steps.push_back(std::abs(wers::test::centsBetween(lastPeriod, reading.period)));
        if (reading.voiced) lastPeriod = reading.period;
        lastVoiced = reading.voiced;
    }
    result.voicedShare = static_cast<double>(loudVoiced) / std::max(1, loud);
    if (!steps.empty()) {
        std::nth_element(steps.begin(), steps.begin() + static_cast<std::ptrdiff_t>(steps.size() / 2), steps.end());
        result.pitchSpreadCents = steps[steps.size() / 2];
    }
}

// Sibilants are found from the frequency balance of each frame, without the classifier the mask uses, so the
// classifier cannot grade itself.
void measureSibilants(const std::vector<float>& mono, double fs, int latency, SongNumbers& result) {
    RefrenParams p;
    p.delta = true;
    p.layer[0].voices = 8;
    p.layer[0].timeMs = 30.0f;
    p.layer[0].width = 1.0f;
    p.consonants = 0.0f;
    const auto open = run(mono, p, fs);
    p.consonants = 1.0f;
    const auto masked = run(mono, p, fs);

    const int frame = std::max(1, static_cast<int>(std::lround(fs * kFrameSeconds)));
    const size_t frames = mono.size() / static_cast<size_t>(frame);
    wers::Biquad low, high;
    low.set(wers::Biquad::Type::LowPass, kLowHz, 0.7071, 0.0, fs);
    high.set(wers::Biquad::Type::HighPass, kHighHz, 0.7071, 0.0, fs);
    std::vector<double> lowEnergy(frames), highEnergy(frames), allEnergy(frames);
    double loudest = 0.0;
    for (size_t f = 0; f < frames; ++f) {
        for (int i = 0; i < frame; ++i) {
            const double v = static_cast<double>(mono[f * static_cast<size_t>(frame) + static_cast<size_t>(i)]);
            const double a = low.tick(v), b = high.tick(v);
            lowEnergy[f] += a * a;
            highEnergy[f] += b * b;
            allEnergy[f] += v * v;
        }
        loudest = std::max(loudest, allEnergy[f]);
    }
    const auto sidesIn = [&](const Stereo& y, size_t f) {
        const size_t start = f * static_cast<size_t>(frame) + static_cast<size_t>(latency);
        const size_t end = std::min(y.left.size(), start + static_cast<size_t>(frame));
        double energy = 0.0;
        for (size_t i = start; i < end; ++i) {
            const double side = 0.5 * (static_cast<double>(y.left[i]) - y.right[i]);
            energy += side * side;
        }
        return energy;
    };
    double openSides = 0.0, maskedSides = 0.0;
    for (size_t f = 0; f < frames; ++f) {
        if (highEnergy[f] >= lowEnergy[f] * fromDb(kSibilantOverLowDb) && allEnergy[f] >= loudest * fromDb(-kSibilantRangeDb)) {
            openSides += sidesIn(open, f);
            maskedSides += sidesIn(masked, f);
        }
    }
    result.sibSideDropDb = toDb(openSides) - toDb(maskedSides);
}

double weightedLevelDb(const std::vector<float>& x, double fs, size_t from, size_t to) {
    wers::KWeighting weight;
    weight.prepare(fs);
    double energy = 0.0;
    for (size_t i = 0; i < to; ++i) {
        const double v = weight.tick(static_cast<double>(x[i]));
        if (i >= from) energy += v * v;
    }
    return toDb(energy / static_cast<double>(std::max<size_t>(1, to - from)));
}

} // namespace

SongNumbers measureVoice(const std::vector<float>& mono, double fs) {
    SongNumbers result;
    result.seconds = static_cast<double>(mono.size()) / fs;
    if (mono.empty() || fs <= 0.0) return result;
    measurePitch(mono, fs, result);
    const int latency = static_cast<int>(std::lround(0.030 * fs));
    measureSibilants(mono, fs, latency, result);
    const auto output = run(mono, RefrenParams{}, fs);
    const size_t settle = std::min(mono.size() / 2, static_cast<size_t>(fs * kSettleSeconds));
    const size_t inputEnd = mono.size() - std::min(mono.size(), static_cast<size_t>(latency));
    result.levelDiffDb = weightedLevelDb(output.left, fs, settle + static_cast<size_t>(latency), output.left.size()) - weightedLevelDb(mono, fs, settle, inputEnd);
    return result;
}

int measureVoiceList(const std::u8string& listPath, const std::u8string& outPath) {
    namespace fs = std::filesystem;
    std::ofstream out(fs::path(outPath), std::ios::binary);
    if (!out) return 0;
    out << "index\tseconds\tvoicedShare\tpitchSpreadCents\tsibSideDropDb\tlevelDiffDb\n";
    return wers::test::forEachListedFile(listPath, [&](int index, const std::vector<float>* mono, int rate) {
        const SongNumbers result = mono != nullptr ? measureVoice(*mono, static_cast<double>(rate)) : SongNumbers{};
        char row[160];
        std::snprintf(row, sizeof(row), "%d\t%.2f\t%.3f\t%.2f\t%.2f\t%.2f\n", index, result.seconds, result.voicedShare, result.pitchSpreadCents, result.sibSideDropDb, result.levelDiffDb);
        out << row;
    });
}

std::vector<double> measureCpu(double seconds) {
    constexpr double fs = 48000.0;
    constexpr int kBlock = 256;
    const auto left = wers::test::loudPhrase(fs, seconds, -6.0, 5u), right = wers::test::loudPhrase(fs, seconds, -6.0, 9u);
    const int n = static_cast<int>(left.size());
    RefrenParams p;
    for (auto& l : p.layer) {
        l.on = true;
        l.voices = 8;
        l.lowHz = 100.0f;
        l.highHz = 12000.0f;
    }
    p.layer[0].semis = 12;
    p.layer[0].formant = -3.0f;
    p.layer[1].semis = -12;
    p.layer[1].formant = 3.0f;
    p.layer[2].semis = 7;
    p.layer[3].source = Source::Whisper;
    p.grainDb = 12.0f;
    p.tilt = 0.2f;
    p.motion = Motion::Flanger;
    p.motionDepth = 0.5f;
    p.echoMix = 0.3f;
    p.verbMix = 0.3f;
    p.verbDecay = 3.0f;
    p.verbShimmer = 0.5f;
    p.sendDry = 0.2f;
    double best = 1.0e9;
    for (int round = 0; round < 3; ++round) {
        RefrenEngine engine;
        engine.prepare(fs);
        auto l = left, r = right;
        const auto start = std::chrono::steady_clock::now();
        for (int from = 0; from + kBlock <= n; from += kBlock) {
            float* channels[] = { l.data() + from, r.data() + from };
            engine.setParams(p);
            engine.process(channels, 2, kBlock);
        }
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }
    return { 100.0 * best / seconds };
}

} // namespace refren
