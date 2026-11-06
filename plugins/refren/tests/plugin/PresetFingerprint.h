#pragma once
// What a preset sounds like, in a few numbers, so two presets that nobody could tell apart fail a test. Measured on
// the output of the processor from the same phrase of a synthetic voice.
#include "Filters.h"
#include "TestSignals.h"
#include "plugin/RefrenProcessor.h"

namespace refren {

struct Fingerprint {
    double width = 0.0, low = 0.0, high = 0.0, tailMs = 0.0, motion = 0.0, half = 0.0, twice = 0.0, density = 0.0;
    // The level of the loudest layer at each interval from -24 to +24, from the settings. An interval adds few new
    // frequencies to a voice, and a shift that keeps the formants adds none where this phrase has nothing to lift, so
    // two different harmonies would measure the same. Nobody hears them as the same.
    std::array<float, 49> intervalDb{};
};

namespace fingerprint {

inline constexpr double kFs = 48000.0;
inline constexpr int kBlock = 256;
inline constexpr double kGateSeconds = 0.2, kPhraseSeconds = 3.0, kMostTailSeconds = 6.0;

// These differences can be heard. They are not lowered to let a preset through: the preset changes instead.
inline constexpr double kWidthDb = 2.0, kLowDb = 1.5, kHighDb = 1.5, kTailMs = 100.0, kMotionDb = 1.5, kHalfDb = 3.0, kTwiceDb = 3.0, kDensityShare = 0.2;
inline constexpr float kNoLayerDb = -60.0f, kIntervalDb = 6.0f;

inline double toDb(double power) { return 10.0 * std::log10(power + 1.0e-30); }

// Two filters in a row, for a steeper edge.
inline double energyThrough(const std::vector<float>& x, int from, int to, double lowCut, double highCut) {
    wers::Biquad a, b, c, d;
    if (lowCut > 0.0) {
        a.set(wers::Biquad::Type::HighPass, lowCut, 0.7071, 0.0, kFs);
        b.set(wers::Biquad::Type::HighPass, lowCut, 0.7071, 0.0, kFs);
    }
    if (highCut > 0.0) {
        c.set(wers::Biquad::Type::LowPass, highCut, 0.7071, 0.0, kFs);
        d.set(wers::Biquad::Type::LowPass, highCut, 0.7071, 0.0, kFs);
    }
    double energy = 0.0;
    for (int i = 0; i < to; ++i) {
        double v = static_cast<double>(x[static_cast<size_t>(i)]);
        if (lowCut > 0.0) v = b.tick(a.tick(v));
        if (highCut > 0.0) v = d.tick(c.tick(v));
        if (i >= from) energy += v * v;
    }
    return energy;
}

inline double energyOf(const std::vector<float>& x, int from, int to) {
    double energy = 0.0;
    for (int i = from; i < to; ++i) energy += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)];
    return energy;
}

} // namespace fingerprint

// Loads the preset, leaves everything else in the processor as it is.
inline Fingerprint measureFingerprintOfCurrent(RefrenProcessor& p) {
    using namespace fingerprint;
    p.prepareToPlay(kFs, kBlock);
    const int gate = static_cast<int>(kGateSeconds * kFs), phraseLength = static_cast<int>(kPhraseSeconds * kFs), tailLength = static_cast<int>(kMostTailSeconds * kFs);
    const int total = gate + phraseLength + tailLength;
    const auto phrase = wers::test::loudPhrase(kFs, kPhraseSeconds, -6.0);
    juce::AudioBuffer<float> audio(2, total);
    audio.clear();
    audio.copyFrom(0, gate, phrase.data(), phraseLength);
    audio.copyFrom(1, gate, phrase.data(), phraseLength);
    juce::MidiBuffer midi;
    for (int from = 0; from < total; from += kBlock) {
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, from, juce::jmin(kBlock, total - from));
        p.processBlock(view, midi);
    }
    const int late = p.getLatencySamples();
    const int start = gate + late, end = gate + late + phraseLength;
    std::vector<float> mid(static_cast<size_t>(total)), side(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        mid[static_cast<size_t>(i)] = 0.5f * (audio.getSample(0, i) + audio.getSample(1, i));
        side[static_cast<size_t>(i)] = 0.5f * (audio.getSample(0, i) - audio.getSample(1, i));
    }
    Fingerprint f;
    const double all = energyOf(mid, start, end) + energyOf(side, start, end);
    f.width = toDb(energyOf(side, start, end)) - toDb(energyOf(mid, start, end));
    const auto shareOfBand = [&](double lowCut, double highCut) {
        return toDb(energyThrough(mid, start, end, lowCut, highCut) + energyThrough(side, start, end, lowCut, highCut)) - toDb(all);
    };
    f.low = shareOfBand(0.0, 200.0);
    f.high = shareOfBand(5000.0, 0.0);
    f.half = shareOfBand(60.0, 90.0);
    f.twice = shareOfBand(240.0, 360.0);

    // Until the sound has fallen 30 dB under the end of the phrase for good, so an echo after a gap still counts.
    const auto levelOf = [&](int from, int length) {
        double energy = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = from; i < from + length; ++i) energy += static_cast<double>(audio.getSample(c, i)) * audio.getSample(c, i);
        return toDb(energy / (2.0 * length));
    };
    const double lastOfPhrase = levelOf(end - static_cast<int>(0.2 * kFs), static_cast<int>(0.2 * kFs));
    const int window = static_cast<int>(0.05 * kFs);
    f.tailMs = 0.0;
    for (int i = end; i + window <= total; i += window / 5)
        if (levelOf(i, window) > lastOfPhrase - 30.0) f.tailMs = 1000.0 * (i + window / 5 - end) / kFs;

    // How much the level of the sides moves around.
    const int moving = static_cast<int>(0.1 * kFs);
    std::vector<double> levels;
    double mean = 0.0;
    for (int i = start; i + moving <= end; i += moving) {
        const double level = juce::jmax(-90.0, toDb(energyOf(side, i, i + moving) / moving));
        levels.push_back(level);
        mean += level;
    }
    mean /= static_cast<double>(levels.size());
    double spread = 0.0;
    for (double level : levels) spread += (level - mean) * (level - mean);
    f.motion = std::sqrt(spread / static_cast<double>(levels.size()));

    // Peaks of the level per second that stand 3 dB over what is around them.
    const int step = static_cast<int>(0.005 * kFs);
    std::vector<double> envelope;
    for (int i = start; i + step <= end; i += step) envelope.push_back(toDb(energyOf(mid, i, i + step) / step));
    const int around = 8;
    int peaks = 0;
    for (int i = around; i + around < static_cast<int>(envelope.size()); ++i) {
        double lowest = envelope[static_cast<size_t>(i)];
        bool highest = true;
        for (int k = i - around; k <= i + around; ++k) {
            if (envelope[static_cast<size_t>(k)] > envelope[static_cast<size_t>(i)]) highest = false;
            lowest = juce::jmin(lowest, envelope[static_cast<size_t>(k)]);
        }
        if (highest && envelope[static_cast<size_t>(i)] - lowest >= 3.0) ++peaks;
    }
    f.density = peaks / kPhraseSeconds;

    f.intervalDb.fill(kNoLayerDb);
    auto& state = p.state();
    for (int k = 0; k < kLayers; ++k) {
        if (state.getRawParameterValue(layerId(k, ids::on))->load() < 0.5f) continue;
        const auto source = static_cast<Source>(juce::roundToInt(state.getRawParameterValue(layerId(k, ids::source))->load()));
        if (source != Source::Voice && source != Source::Sub) continue;
        const int semis = juce::roundToInt(state.getRawParameterValue(layerId(k, ids::semis))->load());
        auto& slot = f.intervalDb[static_cast<size_t>(juce::jlimit(0, 48, semis + 24))];
        slot = juce::jmax(slot, state.getRawParameterValue(layerId(k, ids::level))->load());
    }
    f.intervalDb[24] = kNoLayerDb; // the voice itself is no harmony
    return f;
}

inline Fingerprint measureFingerprint(RefrenProcessor& p, const juce::String& preset) {
    p.presets().load(preset);
    return measureFingerprintOfCurrent(p);
}

// Nothing tells them apart.
inline bool tooSimilar(const Fingerprint& a, const Fingerprint& b) {
    using namespace fingerprint;
    for (size_t k = 0; k < a.intervalDb.size(); ++k)
        if (std::abs(a.intervalDb[k] - b.intervalDb[k]) > kIntervalDb) return false;
    return std::abs(a.width - b.width) <= kWidthDb && std::abs(a.low - b.low) <= kLowDb && std::abs(a.high - b.high) <= kHighDb && std::abs(a.tailMs - b.tailMs) <= kTailMs
        && std::abs(a.motion - b.motion) <= kMotionDb && std::abs(a.half - b.half) <= kHalfDb && std::abs(a.twice - b.twice) <= kTwiceDb
        && std::abs(a.density - b.density) <= kDensityShare * juce::jmax(a.density, b.density, 1.0);
}

} // namespace refren
