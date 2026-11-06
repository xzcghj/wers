// Tests of the factory presets through the same entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "Measure.h"
#include "PresetBar.h"
#include "PresetFingerprint.h"
#include "SharedText.h"
#include "plugin/FactoryPresets.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

using namespace refren;

#ifndef REFREN_PRESET_STEP
 #define REFREN_PRESET_STEP 1
#endif

namespace {

constexpr double kFs = 48000.0;

juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

int latencyFor(double fs) { return static_cast<int>(std::lround(0.030 * fs)); }

void setParam(RefrenProcessor& p, const juce::String& id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(RefrenProcessor& p, const juce::String& id) { return p.state().getRawParameterValue(id)->load(); }

juce::AudioBuffer<float> phraseBuffer(double seconds, double peakDb) {
    const auto x = wers::test::loudPhrase(kFs, seconds, peakDb);
    juce::AudioBuffer<float> b(2, static_cast<int>(x.size()));
    b.copyFrom(0, 0, x.data(), b.getNumSamples());
    b.copyFrom(1, 0, x.data(), b.getNumSamples());
    return b;
}

void runBlocks(RefrenProcessor& p, juce::AudioBuffer<float>& audio, int block = 256) {
    juce::MidiBuffer midi;
    for (int from = 0; from < audio.getNumSamples(); from += block) {
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), from, juce::jmin(block, audio.getNumSamples() - from));
        p.processBlock(view, midi);
    }
}

double weightedDb(const std::vector<float>& x, int from, int to) { return wers::test::weightedDb(x, from, to, kFs); }

std::vector<float> channelOf(const juce::AudioBuffer<float>& b, int c) {
    return std::vector<float>(b.getReadPointer(c), b.getReadPointer(c) + b.getNumSamples());
}

// The loudness of both channels together, as it is heard.
double stereoWeightedDb(const juce::AudioBuffer<float>& b, int from, int to) {
    return 10.0 * std::log10(0.5 * (std::pow(10.0, weightedDb(channelOf(b, 0), from, to) * 0.1) + std::pow(10.0, weightedDb(channelOf(b, 1), from, to) * 0.1)));
}

double peakOf(const juce::AudioBuffer<float>& b, int from, int to) {
    float peak = 1.0e-12f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) peak = juce::jmax(peak, std::abs(b.getSample(c, i)));
    return 20.0 * std::log10(static_cast<double>(peak));
}

std::map<juce::String, float> allValues(RefrenProcessor& p) {
    std::map<juce::String, float> values;
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) values[withId->paramID] = withId->getValue();
    return values;
}

struct TempFolder {
    TempFolder() : path(juce::File::createTempFile("refren_presets")) { path.createDirectory(); }
    ~TempFolder() { path.deleteRecursively(); }
    juce::File path;
};

template <typename Check>
void everyPreset(Check&& check) {
    const auto presets = factoryPresets();
    for (size_t k = 0; k < presets.size(); k += REFREN_PRESET_STEP) check(presets[k]);
}

} // namespace

TEST_CASE("factory presets are valid") {
    const auto presets = factoryPresets();
    RefrenProcessor p;
    juce::StringArray seen;
    const auto excluded = presetExcludedIds();
    for (const auto& preset : presets) {
        const auto name = pl(preset.name);
        CAPTURE(name);
        CHECK(name.length() <= 18);
        CHECK_FALSE(seen.contains(name));
        seen.add(name);
        for (const char* forbidden : { "Nirvana", "Beatles", "Kanye", "Travis", "Drake", "Korg", "Roland", "Moog", "Autotune" }) CHECK_FALSE(name.containsIgnoreCase(forbidden));
        REQUIRE(preset.group != nullptr);
        CHECK(std::any_of(kGroups.begin(), kGroups.end(), [&](const PresetGroup& g) { return std::strcmp(g.name, preset.group) == 0; }));
        REQUIRE(preset.description != nullptr);
        const auto description = pl(preset.description);
        CHECK(description.length() > 10);
        CHECK(description.endsWithChar('.'));
        CHECK_FALSE(description.dropLastCharacters(1).containsChar('.'));
        for (const auto& [id, value] : preset.values) {
            CAPTURE(juce::String(id));
            auto* parameter = p.state().getParameter(id);
            REQUIRE(parameter != nullptr);
            CHECK_FALSE(excluded.contains(id));
            const auto range = p.state().getParameterRange(id);
            CHECK(value >= range.start);
            CHECK(value <= range.end);
        }
    }
    CHECK(excluded.size() == 4);
    for (const char* id : { ids::amount, ids::output, ids::delta, ids::bypass }) CHECK(excluded.contains(id));
}

TEST_CASE("groups hold enough presets") {
    const auto presets = factoryPresets();
    for (const auto& group : kGroups) {
        const auto count = std::count_if(presets.begin(), presets.end(), [&](const auto& preset) { return std::strcmp(preset.group, group.name) == 0; });
        CAPTURE(pl(group.name));
        CHECK(count >= group.atLeast);
    }
    MESSAGE(presets.size() << " presets in " << kGroups.size() << " groups");
}

TEST_CASE("amount zero is neutral on every preset") {
    everyPreset([](const wers::ui::FactoryPreset& preset) {
        CAPTURE(pl(preset.name));
        RefrenProcessor p;
        REQUIRE(p.presets().load(pl(preset.name)));
        setParam(p, ids::amount, 0.0f);
        p.prepareToPlay(kFs, 256);
        const auto in = phraseBuffer(0.5, -6.0);
        auto out = in;
        runBlocks(p, out);
        double worst = 0.0;
        const int late = latencyFor(kFs);
        for (int c = 0; c < 2; ++c)
            for (int i = late; i < in.getNumSamples(); ++i) worst = juce::jmax(worst, std::abs(static_cast<double>(out.getSample(c, i)) - in.getSample(c, i - late)));
        CHECK(20.0 * std::log10(worst + 1.0e-30) < -140.0);
    });
}

TEST_CASE("latency stays with every preset") {
    everyPreset([](const wers::ui::FactoryPreset& preset) {
        CAPTURE(pl(preset.name));
        RefrenProcessor p;
        REQUIRE(p.presets().load(pl(preset.name)));
        setParam(p, ids::amount, 0.0f);
        for (double fs : wers::test::kRates) {
            CAPTURE(fs);
            p.prepareToPlay(fs, 512);
            CHECK(p.getLatencySamples() == latencyFor(fs));
            juce::AudioBuffer<float> impulse(2, static_cast<int>(fs * 0.05));
            impulse.clear();
            impulse.setSample(0, 0, 0.5f);
            runBlocks(p, impulse, 512);
            CHECK(impulse.getSample(0, latencyFor(fs)) == doctest::Approx(0.5f));
        }
    });
}

TEST_CASE("preset loudness") {
    everyPreset([](const wers::ui::FactoryPreset& preset) {
        CAPTURE(pl(preset.name));
        for (double peak : { -12.0, -3.0 }) {
            CAPTURE(peak);
            RefrenProcessor p;
            REQUIRE(p.presets().load(pl(preset.name)));
            p.prepareToPlay(kFs, 256);
            const auto in = phraseBuffer(4.0, peak);
            auto out = in;
            runBlocks(p, out);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < out.getNumSamples(); ++i) REQUIRE(std::isfinite(out.getSample(c, i)));
            const int from = static_cast<int>(kFs), to = out.getNumSamples(), late = latencyFor(kFs);
            const double input = stereoWeightedDb(in, from, to - late), output = stereoWeightedDb(out, from + late, to);
            MESSAGE(pl(preset.name) << " at " << peak << ": level " << output - input << " dB, peak " << peakOf(out, from, to) - peakOf(in, from, to) << " dB");
            CHECK(std::abs(output - input) <= 1.0);
            CHECK(peakOf(out, from, to) - peakOf(in, from, to) <= 3.0);
        }
    });
}

TEST_CASE("mono keeps the level") {
    everyPreset([](const wers::ui::FactoryPreset& preset) {
        CAPTURE(pl(preset.name));
        RefrenProcessor p;
        REQUIRE(p.presets().load(pl(preset.name)));
        p.prepareToPlay(kFs, 256);
        auto out = phraseBuffer(4.0, -6.0);
        runBlocks(p, out);
        std::vector<float> sum(static_cast<size_t>(out.getNumSamples()));
        for (int i = 0; i < out.getNumSamples(); ++i) sum[static_cast<size_t>(i)] = 0.5f * (out.getSample(0, i) + out.getSample(1, i));
        const int from = static_cast<int>(kFs), to = out.getNumSamples();
        const double stereo = stereoWeightedDb(out, from, to), mono = weightedDb(sum, from, to);
        MESSAGE(pl(preset.name) << ": mono " << mono - stereo << " dB");
        CHECK(mono >= stereo - 3.0);
    });
}

TEST_CASE("presets differ from each other") {
    const auto presets = factoryPresets();
    std::vector<Fingerprint> prints;
    for (const auto& preset : presets) {
        RefrenProcessor p;
        prints.push_back(measureFingerprint(p, pl(preset.name)));
    }
    const bool show = std::getenv("REFREN_SHOW_PRINTS") != nullptr;
    for (size_t a = 0; show && a < presets.size(); ++a) {
        const auto& f = prints[a];
        MESSAGE("print " << pl(presets[a].name) << ": width " << f.width << " low " << f.low << " high " << f.high << " tail " << f.tailMs << " motion " << f.motion << " half " << f.half << " twice " << f.twice << " density " << f.density);
    }
    juce::StringArray pairs;
    for (size_t a = 0; a < presets.size(); ++a)
        for (size_t b = a + 1; b < presets.size(); ++b)
            if (tooSimilar(prints[a], prints[b])) pairs.add(pl(presets[a].name) + " = " + pl(presets[b].name));
    CAPTURE(pairs.joinIntoString(", "));
    CHECK(pairs.isEmpty());
}

TEST_CASE("fingerprint tells presets apart") {
    const auto name = pl(factoryPresets()[0].name);
    RefrenProcessor a, b;
    CHECK(tooSimilar(measureFingerprint(a, name), measureFingerprint(b, name)));
    const auto withChange = [&](const juce::String& id, float value) {
        RefrenProcessor c;
        c.presets().load(name);
        setParam(c, ids::dry, -48.0f); // so what changes is what is heard
        setParam(c, ids::verbMix, 100.0f);
        setParam(c, ids::verbDecay, 1.5f);
        const auto before = measureFingerprintOfCurrent(c);
        setParam(c, id, value);
        return std::pair<Fingerprint, Fingerprint>(before, measureFingerprintOfCurrent(c));
    };
    const auto filtered = withChange(layerId(0, ids::high), 3000.0f);
    MESSAGE("high from " << filtered.first.high << " to " << filtered.second.high);
    CHECK_FALSE(tooSimilar(filtered.first, filtered.second));
    const auto longer = withChange(ids::verbDecay, 1.8f);
    MESSAGE("tail from " << longer.first.tailMs << " to " << longer.second.tailMs << " ms");
    CHECK_FALSE(tooSimilar(longer.first, longer.second));
}

TEST_CASE("loading a preset does not click") {
    const auto presets = factoryPresets();
    const int n = static_cast<int>(kFs * 3.0), change = 256 * 187;
    // On a sine a click stands out, and on a phrase the voices have something to work with.
    juce::AudioBuffer<float> sine(2, n);
    const auto tone = wers::test::sine(150.0, -12.0, n, kFs);
    sine.copyFrom(0, 0, tone.data(), n);
    sine.copyFrom(1, 0, tone.data(), n);
    for (const auto& input : { phraseBuffer(3.0, -6.0), sine }) {
        const auto run = [&](const juce::String& next, const juce::String& first, bool wetAlone) {
            RefrenProcessor p;
            p.presets().load(first);
            setParam(p, ids::delta, wetAlone ? 1.0f : 0.0f);
            p.prepareToPlay(kFs, 256);
            juce::AudioBuffer<float> audio;
            audio.makeCopyOf(input);
            juce::MidiBuffer midi;
            for (int from = 0; from < n; from += 256) {
                if (from == change && next.isNotEmpty()) p.presets().load(next);
                juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, from, juce::jmin(256, n - from));
                p.processBlock(view, midi);
            }
            return audio;
        };
        const auto worst = [&](const juce::AudioBuffer<float>& audio, int channel) {
            double step = 0.0;
            for (int i = change; i < n; ++i) step = juce::jmax(step, std::abs(static_cast<double>(audio.getSample(channel, i)) - audio.getSample(channel, i - 1)));
            return step;
        };
        const auto first = pl(presets.front().name);
        const double steadyAll = worst(run({}, first, false), 0);
        const auto steadyFirst = run({}, first, true);
        const double steadyWet = juce::jmax(worst(steadyFirst, 0), worst(steadyFirst, 1));
        for (size_t k = 1; k < presets.size(); k += static_cast<size_t>(REFREN_PRESET_STEP) * 3) {
            const auto next = pl(presets[k].name);
            CAPTURE(next);
            const auto all = run(next, first, false);
            const auto wet = run(next, first, true);
            CHECK(worst(all, 0) <= 2.0 * juce::jmax(steadyAll, worst(run({}, next, false), 0)));
            // Without the dry voice a click has nothing to hide behind.
            const auto steadyNext = run({}, next, true);
            const double wetLimit = 2.0 * juce::jmax(steadyWet, worst(steadyNext, 0), worst(steadyNext, 1));
            CHECK(worst(wet, 0) <= wetLimit);
            CHECK(worst(wet, 1) <= wetLimit);
            // What is left without the wet sound is the dry voice, and the gate does not touch it.
            juce::AudioBuffer<float> dry;
            dry.makeCopyOf(all);
            dry.addFrom(0, 0, wet, 0, 0, n, -1.0f);
            CHECK(worst(dry, 0) <= 2.0 * worst(input, 0));
        }
    }
}

TEST_CASE("a preset leaves four things alone") {
    RefrenProcessor p;
    setParam(p, ids::amount, 40.0f);
    setParam(p, ids::output, -5.0f);
    setParam(p, ids::delta, 1.0f);
    setParam(p, ids::bypass, 1.0f);
    REQUIRE(p.presets().load(pl(factoryPresets().back().name)));
    CHECK(getParam(p, ids::amount) == doctest::Approx(40.0f));
    CHECK(getParam(p, ids::output) == doctest::Approx(-5.0f));
    CHECK(getParam(p, ids::delta) == doctest::Approx(1.0f));
    CHECK(getParam(p, ids::bypass) == doctest::Approx(1.0f));
}

TEST_CASE("a new instance starts on Luźny") {
    const auto loose = pl("Luźny");
    RefrenProcessor fresh;
    CHECK(fresh.presets().currentName() == loose);
    RefrenProcessor loaded;
    REQUIRE(loaded.presets().load(loose));
    CHECK(allValues(fresh) == allValues(loaded));
    CHECK(getParam(fresh, layerId(0, ids::layerTime)) == doctest::Approx(14.0f));

    // What a project holds comes first.
    RefrenProcessor other;
    const auto otherName = pl(factoryPresets()[0].name);
    REQUIRE(otherName != loose);
    REQUIRE(other.presets().load(otherName));
    setParam(other, ids::tilt, 25.0f);
    juce::MemoryBlock saved;
    other.getStateInformation(saved);
    RefrenProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(restored.presets().currentName() == otherName);
    CHECK(allValues(restored) == allValues(other));
}

TEST_CASE("a project keeps a preset that is gone") {
    RefrenProcessor a;
    setParam(a, ids::tilt, 37.0f);
    setParam(a, layerId(0, ids::voices), 7.0f);
    auto tree = a.presets().toTree();
    tree.setProperty("current", juce::String::fromUTF8("Dawno usunięty"), nullptr);
    a.presets().fromTree(tree);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);

    RefrenProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(allValues(a) == allValues(b));
    const auto gone = juce::String::fromUTF8("Dawno usunięty");
    CHECK(b.presets().currentName() == gone);
    wers::ui::PresetBar bar(b.presets());
    bar.refresh();
    CHECK(bar.shownName() == gone);
    bar.step(1);
    CHECK(b.presets().names().contains(b.presets().currentName()));
    bar.step(-1);
    CHECK(b.presets().names().contains(b.presets().currentName()));
}

TEST_CASE("the store tells about a factory preset") {
    TempFolder folder;
    RefrenProcessor p;
    wers::ui::PresetStore store(p.state(), folder.path, factoryPresets(), presetExcludedIds());
    const auto* info = store.factoryInfo(pl("Luźny"));
    REQUIRE(info != nullptr);
    CHECK(pl(info->group) == "Dubel");
    CHECK(pl(info->description).isNotEmpty());
    CHECK(store.factoryInfo("nie ma takiego") == nullptr);
    CHECK(store.userNames().isEmpty());
    juce::String error;
    REQUIRE(store.save(juce::String::fromUTF8("Mój własny"), error));
    CHECK(store.userNames() == juce::StringArray{ juce::String::fromUTF8("Mój własny") });
    CHECK(store.names().contains(juce::String::fromUTF8("Mój własny")));
    store.setShownName(juce::String::fromUTF8("Mój własny*"));
    CHECK(store.currentName() == juce::String::fromUTF8("Mój własny*"));
}

TEST_CASE("awkward preset files") {
    const auto polish = juce::String::fromUTF8("Żółć ą ę");
    TempFolder folder;
    RefrenProcessor p;
    wers::ui::PresetStore store(p.state(), folder.path, factoryPresets(), presetExcludedIds());
    juce::String error;
    SUBCASE("a Polish name") {
        setParam(p, ids::grain, 9.0f);
        REQUIRE(store.save(polish, error));
        CHECK(store.names().contains(polish));
        setParam(p, ids::grain, 1.0f);
        REQUIRE(store.load(polish));
        CHECK(getParam(p, ids::grain) == doctest::Approx(9.0f));
        CHECK(store.remove(polish));
    }
    SUBCASE("a folder that does not exist yet is created") {
        wers::ui::PresetStore later(p.state(), folder.path.getChildFile("a").getChildFile("b"), factoryPresets(), presetExcludedIds());
        CHECK(later.names().size() == static_cast<int>(factoryPresets().size()));
        CHECK(later.save("nowy", error));
        CHECK(later.names().contains("nowy"));
    }
    SUBCASE("a folder that cannot be made shows a message on the bar") {
        const auto file = folder.path.getChildFile("to jest plik");
        REQUIRE(file.create().wasOk());
        wers::ui::PresetStore blocked(p.state(), file.getChildFile("presety"), factoryPresets(), presetExcludedIds());
        wers::ui::PresetBar bar(blocked);
        bar.saveAs("x");
        CHECK(bar.shownName() == juce::String::fromUTF8(wers::ui::text::presetNoFolder));
    }
    SUBCASE("a damaged file shows a message on the bar") {
        REQUIRE(folder.path.getChildFile("zepsuty.xml").replaceWithText("<WERS_PRESET plugin=\"REFREN\" version=\"1\"><PARAM id="));
        CHECK_FALSE(store.names().contains("zepsuty"));
        wers::ui::PresetBar bar(store);
        bar.loadNamed("zepsuty");
        CHECK(bar.shownName() == juce::String::fromUTF8(wers::ui::text::presetNoRead));
    }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
