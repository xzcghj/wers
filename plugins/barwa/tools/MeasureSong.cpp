#include "MeasureSong.h"
#include "BarwaEngine.h"
#include "TestSignals.h"
#include "WavIO.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace barwa {

namespace {

constexpr double kFrameSeconds = 0.02;
constexpr double kSibilantOverLowDb = 6.0, kSibilantRangeDb = 40.0;
constexpr double kVowelOverHighDb = 12.0, kVowelRangeDb = 25.0;
constexpr double kLowHz = 1000.0, kHighHz = 4000.0;
constexpr double kSettleSeconds = 1.0;

double fromDb(double db) { return std::pow(10.0, db * 0.1); }
double toDb(double power) { return 10.0 * std::log10(power + 1.0e-30); }

std::vector<float> run(const std::vector<float>& x, const BarwaParams& p, double fs, int& latency) {
    BarwaEngine engine;
    engine.prepare(fs, 2);
    engine.setParams(p);
    latency = engine.latencySamples();
    std::vector<float> y = x;
    for (size_t from = 0; from < y.size(); from += 1024) {
        float* channels[] = { y.data() + from };
        engine.process(channels, 1, static_cast<int>(std::min<size_t>(1024, y.size() - from)));
    }
    return y;
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

double peakOf(const std::vector<float>& x, size_t from, size_t to) {
    float peak = 0.0f;
    for (size_t i = from; i < to; ++i) peak = std::max(peak, std::abs(x[i]));
    return static_cast<double>(peak);
}

} // namespace

SongResult measureSong(const std::vector<float>& mono, double fs) {
    SongResult result;
    result.seconds = static_cast<double>(mono.size()) / fs;
    if (mono.empty() || fs <= 0.0) return result;

    BarwaParams p;
    p.band[0].character = Character::Lamp;
    p.band[0].driveDb = 12.0f;
    p.autoLevel = false;
    p.delta = true;
    p.mask = 0.0f;
    int latency = 0;
    const auto open = run(mono, p, fs, latency);
    p.mask = 1.0f;
    const auto masked = run(mono, p, fs, latency);
    p.delta = false;
    p.autoLevel = true;
    const auto output = run(mono, p, fs, latency);

    // The balance of every frame of the input.
    const int frame = std::max(1, static_cast<int>(std::lround(fs * kFrameSeconds)));
    wers::Biquad low, high;
    low.set(wers::Biquad::Type::LowPass, kLowHz, 0.7071, 0.0, fs);
    high.set(wers::Biquad::Type::HighPass, kHighHz, 0.7071, 0.0, fs);
    const size_t frames = mono.size() / static_cast<size_t>(frame);
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

    double sibOpen = 0.0, sibMasked = 0.0, vowelOpen = 0.0, vowelMasked = 0.0;
    double sibInput = 0.0, sibOutput = 0.0, vowelInput = 0.0, vowelOutput = 0.0;
    const auto energyIn = [&](const std::vector<float>& y, size_t f) {
        const size_t start = f * static_cast<size_t>(frame) + static_cast<size_t>(latency);
        const size_t end = std::min(y.size(), start + static_cast<size_t>(frame));
        double energy = 0.0;
        for (size_t i = start; i < end; ++i) energy += static_cast<double>(y[i]) * y[i];
        return energy;
    };
    for (size_t f = 0; f < frames; ++f) {
        if (highEnergy[f] >= lowEnergy[f] * fromDb(kSibilantOverLowDb) && allEnergy[f] >= loudest * fromDb(-kSibilantRangeDb)) {
            ++result.sibFrames;
            sibOpen += energyIn(open, f);
            sibMasked += energyIn(masked, f);
            sibInput += allEnergy[f];
            sibOutput += energyIn(output, f);
        } else if (lowEnergy[f] >= highEnergy[f] * fromDb(kVowelOverHighDb) && allEnergy[f] >= loudest * fromDb(-kVowelRangeDb)) {
            ++result.vowelFrames;
            vowelOpen += energyIn(open, f);
            vowelMasked += energyIn(masked, f);
            vowelInput += allEnergy[f];
            vowelOutput += energyIn(output, f);
        }
    }
    result.sibDropDb = toDb(sibOpen) - toDb(sibMasked);
    result.vowelDropDb = toDb(vowelOpen) - toDb(vowelMasked);
    result.sibLevelDb = toDb(sibOutput) - toDb(sibInput);
    result.vowelLevelDb = toDb(vowelOutput) - toDb(vowelInput);

    const size_t settle = std::min(mono.size() / 2, static_cast<size_t>(fs * kSettleSeconds));
    const size_t inputEnd = mono.size() - std::min(mono.size(), static_cast<size_t>(latency));
    result.levelDiffDb = weightedLevelDb(output, fs, settle + static_cast<size_t>(latency), output.size()) - weightedLevelDb(mono, fs, settle, inputEnd);
    result.peakDiffDb = 20.0 * std::log10((peakOf(output, settle + static_cast<size_t>(latency), output.size()) + 1.0e-12) / (peakOf(mono, settle, inputEnd) + 1.0e-12));
    return result;
}

int measureList(const std::u8string& listPath, const std::u8string& outPath) {
    namespace fs = std::filesystem;
    std::ifstream list(fs::path(listPath), std::ios::binary);
    std::ofstream out(fs::path(outPath), std::ios::binary);
    if (!list || !out) return 0;
    out << "index\tseconds\tsibFrames\tvowelFrames\tsibDropDb\tvowelDropDb\tsibLevelDb\tvowelLevelDb\tlevelDiffDb\tpeakDiffDb\n";
    int index = 0, measured = 0;
    std::string line;
    while (std::getline(list, line)) {
        if (index == 0 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        ++index;
        SongResult result;
        try {
            // On Windows a line that is not valid UTF-8 throws here, and it still gets its row.
            std::ifstream file(fs::path(std::u8string(reinterpret_cast<const char8_t*>(line.data()), line.size())), std::ios::binary);
            if (file) {
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                wers::wav::Audio audio;
                if (wers::wav::readBytes(bytes, audio) && !audio.ch.empty() && !audio.ch[0].empty()) {
                    bytes.clear();
                    bytes.shrink_to_fit();
                    std::vector<float> mono(audio.ch[0].size(), 0.0f);
                    const float share = 1.0f / static_cast<float>(audio.ch.size());
                    for (const auto& channel : audio.ch)
                        for (size_t i = 0; i < mono.size(); ++i) mono[i] += channel[i] * share;
                    result = measureSong(mono, static_cast<double>(audio.rate));
                    ++measured;
                }
            }
        } catch (const std::exception&) {
            result = SongResult{};
        }
        char row[256];
        std::snprintf(row, sizeof(row), "%d\t%.2f\t%d\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\n", index, result.seconds, result.sibFrames, result.vowelFrames,
                      result.sibDropDb, result.vowelDropDb, result.sibLevelDb, result.vowelLevelDb, result.levelDiffDb, result.peakDiffDb);
        out << row;
    }
    return measured;
}

std::vector<double> measureCpu(double seconds) {
    constexpr double fs = 48000.0;
    constexpr int kBlock = 256;
    const int n = static_cast<int>(fs * seconds);
    const auto left = wers::test::noise(-18.0, n, 1u), right = wers::test::noise(-18.0, n, 2u);
    std::vector<double> results;
    for (Character character : { Character::Lamp, Character::Tape, Character::Transformer, Character::Transistor }) {
        BarwaParams p;
        p.bands = 3;
        p.tone = 0.3f;
        for (auto& b : p.band) {
            b.character = character;
            b.driveDb = 12.0f;
        }
        double best = 1.0e9;
        for (int round = 0; round < 3; ++round) {
            BarwaEngine engine;
            engine.prepare(fs, 2);
            auto l = left, r = right;
            const auto start = std::chrono::steady_clock::now();
            for (int from = 0; from + kBlock <= n; from += kBlock) {
                float* channels[] = { l.data() + from, r.data() + from };
                engine.setParams(p);
                engine.process(channels, 2, kBlock);
            }
            best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        }
        results.push_back(100.0 * best / seconds);
    }
    return results;
}

} // namespace barwa
