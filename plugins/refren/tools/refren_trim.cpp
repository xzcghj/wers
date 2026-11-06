// Sets how loud each factory preset is: measures it with the trim at zero and writes what brings it to the level of the
// voice that goes in, from two phrases, one quiet and one loud.
//   refren_trim <path of PresetTrims.inc>
// Or says what each one costs, and fails above the limit of the series:
//   refren_trim --load
#include "FactoryPresets.h"
#include "RefrenProcessor.h"
#include "TestSignals.h"
#include "Measure.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {

constexpr double kFs = 48000.0, kSeconds = 4.0;

double errorDb(refren::RefrenProcessor& p, double peak) {
    const auto x = wers::test::loudPhrase(kFs, kSeconds, peak);
    p.prepareToPlay(kFs, 256);
    const int n = static_cast<int>(x.size());
    juce::AudioBuffer<float> audio(2, n);
    audio.copyFrom(0, 0, x.data(), n);
    audio.copyFrom(1, 0, x.data(), n);
    juce::MidiBuffer midi;
    for (int from = 0; from < n; from += 256) {
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, from, juce::jmin(256, n - from));
        p.processBlock(view, midi);
    }
    const int settled = static_cast<int>(kFs), late = p.getLatencySamples();
    const auto levelOf = [&](int channel) {
        return wers::test::weightedDb(std::vector<float>(audio.getReadPointer(channel), audio.getReadPointer(channel) + n), settled + late, n, kFs);
    };
    const double output = 10.0 * std::log10(0.5 * (std::pow(10.0, levelOf(0) * 0.1) + std::pow(10.0, levelOf(1) * 0.1)));
    return output - wers::test::weightedDb(x, settled, n - late, kFs);
}

constexpr double kLoadLimitPercent = 5.0, kLoadSeconds = 10.0;

// The best of three runs of a voice-like phrase, in the way refren_measure does it for the heaviest settings.
int measureLoad() {
    const auto left = wers::test::loudPhrase(kFs, kLoadSeconds, -6.0, 5u), right = wers::test::loudPhrase(kFs, kLoadSeconds, -6.0, 9u);
    const int n = static_cast<int>(left.size());
    double worst = 0.0;
    juce::String worstName;
    for (const auto& preset : refren::factoryPresets()) {
        refren::RefrenProcessor p;
        const auto name = juce::String::fromUTF8(preset.name);
        if (!p.presets().load(name)) return 1;
        double best = 1.0e9;
        for (int round = 0; round < 3; ++round) {
            p.prepareToPlay(kFs, 256);
            juce::AudioBuffer<float> audio(2, n);
            audio.copyFrom(0, 0, left.data(), n);
            audio.copyFrom(1, 0, right.data(), n);
            juce::MidiBuffer midi;
            const auto start = std::chrono::steady_clock::now();
            for (int from = 0; from + 256 <= n; from += 256) {
                juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, from, 256);
                p.processBlock(view, midi);
            }
            best = juce::jmin(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        }
        const double percent = 100.0 * best / kLoadSeconds;
        if (percent > worst) {
            worst = percent;
            worstName = name;
        }
    }
    std::printf("heaviest preset: %.2f %% of one core, limit %.2f %%\n", worst, kLoadLimitPercent);
    return worst <= kLoadLimitPercent ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: refren_trim <path of PresetTrims.inc> | --load\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (std::strcmp(argv[1], "--load") == 0) return measureLoad();
    juce::String text;
    int outOfRange = 0;
    for (const auto& preset : refren::factoryPresets()) {
        refren::RefrenProcessor p;
        if (!p.presets().load(juce::String::fromUTF8(preset.name))) return 1;
        auto* trim = p.state().getParameter(refren::ids::trim);
        trim->setValueNotifyingHost(trim->convertTo0to1(0.0f));
        const double mean = 0.5 * (errorDb(p, -12.0) + errorDb(p, -3.0));
        const double wanted = std::round(-mean * 10.0) / 10.0 + 0.0; // never a minus zero
        if (wanted < -24.0 || wanted > 12.0) ++outOfRange;
        text << "    { \"" << juce::String::fromUTF8(preset.name) << "\", " << juce::String(wanted, 1) << "f },\n";
    }
    if (outOfRange > 0) std::printf("%d presets need more than the trim can give\n", outOfRange);
    if (!juce::File(juce::String::fromUTF8(argv[1])).replaceWithText(text, false, false, "\n")) return 1;
    return outOfRange > 0 ? 1 : 0;
}
