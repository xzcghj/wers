// Tests of the whole plugin through the same entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "Measure.h"
#include "MeasureVoice.h"
#include "TestSignals.h"
#include "WavIO.h"
#include "PresetBar.h"
#include "plugin/RefrenProcessor.h"
#include "ui/Favorites.h"
#include "ui/LayerColumn.h"
#include "ui/PresetBrowser.h"
#include "ui/RefrenEditor.h"
#include "ui/StageView.h"
#include "ui/Strings.h"
#include "SharedText.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <thread>

using namespace refren;

// GCC sees these replacements of new and delete inlined into each other and takes malloc and free for a mismatch.
#if defined(__GNUC__) && !defined(__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// Only the thread under test counts: the timer thread of JUCE allocates now and then by itself.
static std::atomic<long> gAllocations{ 0 };
static thread_local bool gCount = false;
void* operator new(std::size_t n) {
    if (gCount) gAllocations.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr double kFs = 48000.0;

int latencyFor(double fs) { return static_cast<int>(std::lround(0.030 * fs)); }

void setParam(RefrenProcessor& p, const juce::String& id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(RefrenProcessor& p, const juce::String& id) { return p.state().getRawParameterValue(id)->load(); }

juce::AudioBuffer<float> noiseBuffer(int channels, int n, float gain = 0.1f, int seed = 1) {
    juce::AudioBuffer<float> b(channels, n);
    juce::Random r(seed);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i) b.setSample(c, i, (r.nextFloat() * 2.0f - 1.0f) * gain);
    return b;
}

juce::AudioBuffer<float> sineBuffer(int channels, int n, double hz = 150.0, double db = -12.0, double fs = kFs) {
    const auto x = wers::test::sine(hz, db, n, fs);
    juce::AudioBuffer<float> b(channels, n);
    for (int c = 0; c < channels; ++c) b.copyFrom(c, 0, x.data(), n);
    return b;
}

void runBlocks(RefrenProcessor& p, juce::AudioBuffer<float>& audio, int block) {
    juce::MidiBuffer midi;
    for (int from = 0; from < audio.getNumSamples(); from += block) {
        const int n = juce::jmin(block, audio.getNumSamples() - from);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), from, n);
        p.processBlock(view, midi);
    }
}

double worstError(const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency, int channelsOut = -1) {
    double worst = 0.0;
    for (int c = 0; c < (channelsOut < 0 ? in.getNumChannels() : channelsOut); ++c)
        for (int i = latency; i < in.getNumSamples(); ++i)
            worst = juce::jmax(worst, std::abs(static_cast<double>(out.getSample(c, i)) - in.getSample(juce::jmin(c, in.getNumChannels() - 1), i - latency)));
    return worst;
}

double worstStep(const juce::AudioBuffer<float>& audio, int channel, int from, int to) {
    double worst = 0.0;
    for (int i = juce::jmax(1, from); i < to; ++i) worst = juce::jmax(worst, std::abs(static_cast<double>(audio.getSample(channel, i)) - audio.getSample(channel, i - 1)));
    return worst;
}

bool exactly(float a, float b) { return std::equal_to<float>{}(a, b); }

bool allFinite(const juce::AudioBuffer<float>& audio) {
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (!std::isfinite(audio.getSample(c, i))) return false;
    return true;
}

std::map<juce::String, float> allValues(RefrenProcessor& p) {
    std::map<juce::String, float> values;
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) values[withId->paramID] = withId->getValue();
    return values;
}

void writeInt(char* dest, int value) {
    const auto stored = juce::ByteOrder::swapIfBigEndian(static_cast<juce::uint32>(value));
    std::memcpy(dest, &stored, sizeof(stored));
}

// The saved form: magic, version, length of the text, the text.
juce::MemoryBlock packState(int version, const juce::String& xml) {
    juce::MemoryBlock block;
    juce::MemoryOutputStream out(block, false);
    out.write("WRFN", 4);
    out.writeInt(version);
    const auto text = xml.toRawUTF8();
    const auto length = xml.getNumBytesAsUTF8();
    out.writeInt(static_cast<int>(length));
    out.write(text, length);
    out.flush();
    return block;
}

juce::String stateText(RefrenProcessor& p) {
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    const auto length = static_cast<size_t>(juce::ByteOrder::littleEndianInt(static_cast<const char*>(saved.getData()) + 8));
    return juce::String::fromUTF8(static_cast<const char*>(saved.getData()) + 12, static_cast<int>(length));
}

// A host that says what the tempo is.
struct FixedTempo final : juce::AudioPlayHead {
    explicit FixedTempo(double b) : bpm(b) {}
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        if (bpm > 0.0) info.setBpm(bpm);
        return info;
    }
    double bpm;
};

int peakPlace(const juce::AudioBuffer<float>& audio, int channel) {
    int best = 0;
    float highest = 0.0f;
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        const float v = std::abs(audio.getSample(channel, i));
        if (v > highest) {
            highest = v;
            best = i;
        }
    }
    return best;
}

// Only the echo of the dry sound comes out, with one repeat.
void onlyTheEcho(RefrenProcessor& p) {
    setParam(p, layerId(0, ids::on), 0.0f);
    setParam(p, ids::delta, 1.0f);
    setParam(p, ids::sendDry, 100.0f);
    setParam(p, ids::echoMix, 100.0f);
    setParam(p, ids::echoFeedback, 0.0f);
    setParam(p, ids::echoTone, 12000.0f);
    setParam(p, ids::duck, 0.0f);
}

struct TempFolder {
    TempFolder() : path(juce::File::createTempFile("refren_screen")) { path.createDirectory(); }
    ~TempFolder() { path.deleteRecursively(); }
    juce::File path;
};

juce::AudioBuffer<float> impulseBuffer(int n) {
    juce::AudioBuffer<float> b(2, n);
    b.clear();
    b.setSample(0, 0, 0.5f);
    b.setSample(1, 0, 0.5f);
    return b;
}

} // namespace

TEST_CASE("parameters match the specification") {
    RefrenProcessor p;
    const auto check = [&](const juce::String& id, float low, float high, float initial, bool automatable) {
        CAPTURE(id);
        auto* param = p.state().getParameter(id);
        REQUIRE(param != nullptr);
        auto range = p.state().getParameterRange(id);
        CHECK(range.start == doctest::Approx(low));
        CHECK(range.end == doctest::Approx(high));
        CHECK(range.convertFrom0to1(param->getDefaultValue()) == doctest::Approx(initial));
        CHECK(param->isAutomatable() == automatable);
    };
    check(ids::amount, 0.0f, 100.0f, 100.0f, true);
    check(ids::width, 0.0f, 200.0f, 100.0f, true);
    check(ids::time, 0.0f, 200.0f, 100.0f, true);
    check(ids::tilt, -100.0f, 100.0f, 0.0f, true);
    check(ids::grain, 0.0f, 36.0f, 0.0f, true);
    check(ids::grainChar, 0.0f, 3.0f, 0.0f, false);
    check(ids::motion, 0.0f, 3.0f, 0.0f, false);
    check(ids::motionRate, 0.05f, 10.0f, 1.0f, true);
    check(ids::motionDepth, 0.0f, 100.0f, 0.0f, true);
    check(ids::echoMix, 0.0f, 100.0f, 0.0f, true);
    check(ids::echoTime, 0.0f, 6.0f, 1.0f, false);
    check(ids::echoFeedback, 0.0f, 90.0f, 25.0f, true);
    check(ids::echoPing, 0.0f, 1.0f, 0.0f, false);
    check(ids::echoTone, 500.0f, 12000.0f, 6000.0f, true);
    check(ids::verbMix, 0.0f, 100.0f, 0.0f, true);
    check(ids::verbSize, 0.0f, 100.0f, 50.0f, false);
    check(ids::verbDecay, 0.2f, 8.0f, 1.5f, true);
    check(ids::verbTone, 1000.0f, 16000.0f, 6000.0f, true);
    check(ids::verbPre, 0.0f, 80.0f, 10.0f, true);
    check(ids::verbShimmer, 0.0f, 100.0f, 0.0f, true);
    check(ids::duck, 0.0f, 100.0f, 50.0f, true);
    check(ids::sendDry, 0.0f, 100.0f, 0.0f, true);
    check(ids::consonants, 0.0f, 100.0f, 100.0f, true);
    check(ids::dry, -48.0f, 0.0f, 0.0f, true);
    check(ids::trim, -24.0f, 12.0f, 0.0f, false);
    check(ids::output, -24.0f, 24.0f, 0.0f, true);
    check(ids::delta, 0.0f, 1.0f, 0.0f, false);
    check(ids::bypass, 0.0f, 1.0f, 0.0f, true);
    for (int k = 0; k < kLayers; ++k) {
        check(layerId(k, ids::on), 0.0f, 1.0f, k == 0 ? 1.0f : 0.0f, false);
        check(layerId(k, ids::source), 0.0f, 3.0f, 0.0f, false);
        check(layerId(k, ids::voices), 1.0f, 16.0f, 2.0f, false);
        check(layerId(k, ids::semis), -24.0f, 24.0f, 0.0f, false);
        check(layerId(k, ids::formant), -12.0f, 12.0f, 0.0f, false);
        check(layerId(k, ids::level), -48.0f, 6.0f, -6.0f, true);
        check(layerId(k, ids::layerWidth), 0.0f, 100.0f, 50.0f, true);
        check(layerId(k, ids::layerTime), 0.0f, 40.0f, 12.0f, true);
        check(layerId(k, ids::detune), 0.0f, 50.0f, 8.0f, true);
        check(layerId(k, ids::low), 20.0f, 2000.0f, 20.0f, true);
        check(layerId(k, ids::high), 1000.0f, 20000.0f, 20000.0f, true);
        check(layerId(k, ids::note), 36.0f, 72.0f, 48.0f, false);
    }
    CHECK(layerId(0, ids::on) == "l1On");
    CHECK(layerId(3, ids::note) == "l4Note");
    CHECK(p.getParameters().size() == 76);
    CHECK(p.getBypassParameter() == p.state().getParameter(ids::bypass));
    CHECK(p.getName() == "WERS Refren");
    CHECK(RefrenProcessor::kStateVersion == 1);
}

TEST_CASE("every parameter reaches the engine") {
    // Every value differs from every other and every one is heard, so a parameter wired to the wrong place changes
    // the result. What the test sets by identifier has to come out the same as the engine set by name.
    RefrenProcessor plugin;
    plugin.prepareToPlay(kFs, 256);
    const auto set = [&](const juce::String& id, float value) {
        setParam(plugin, id, value);
        return getParam(plugin, id);
    };
    RefrenParams e;
    e.amount = set(ids::amount, 83.0f) * 0.01f;
    e.widthScale = set(ids::width, 170.0f) * 0.01f;
    e.timeScale = set(ids::time, 130.0f) * 0.01f;
    e.tilt = set(ids::tilt, 23.0f) * 0.01f;
    e.grainDb = set(ids::grain, 11.0f);
    e.grainChar = static_cast<Character>(juce::roundToInt(set(ids::grainChar, 2.0f)));
    e.motion = static_cast<Motion>(juce::roundToInt(set(ids::motion, 3.0f)));
    e.motionRate = set(ids::motionRate, 2.3f);
    e.motionDepth = set(ids::motionDepth, 47.0f) * 0.01f;
    e.echoMix = set(ids::echoMix, 37.0f) * 0.01f;
    e.echoTime = static_cast<EchoTime>(juce::roundToInt(set(ids::echoTime, 2.0f)));
    e.echoFeedback = set(ids::echoFeedback, 41.0f) * 0.01f;
    e.echoPing = set(ids::echoPing, 1.0f) > 0.5f;
    e.echoToneHz = set(ids::echoTone, 3100.0f);
    e.verbMix = set(ids::verbMix, 29.0f) * 0.01f;
    e.verbSize = set(ids::verbSize, 61.0f) * 0.01f;
    e.verbDecay = set(ids::verbDecay, 2.7f);
    e.verbToneHz = set(ids::verbTone, 4300.0f);
    e.verbPreMs = set(ids::verbPre, 17.0f);
    e.verbShimmer = set(ids::verbShimmer, 53.0f) * 0.01f;
    e.duck = set(ids::duck, 67.0f) * 0.01f;
    e.sendDry = set(ids::sendDry, 19.0f) * 0.01f;
    e.consonants = set(ids::consonants, 71.0f) * 0.01f;
    e.dryDb = set(ids::dry, -3.3f);
    e.trimDb = set(ids::trim, -1.7f);
    e.outputDb = set(ids::output, -2.9f);
    const std::array<Source, 4> sources{ Source::Voice, Source::Robot, Source::Sub, Source::Whisper };
    for (int k = 0; k < kLayers; ++k) {
        auto& l = e.layer[static_cast<size_t>(k)];
        const auto at = [k](const char* suffix) { return layerId(k, suffix); };
        const auto shift = static_cast<float>(k);
        l.on = set(at(ids::on), 1.0f) > 0.5f;
        l.source = static_cast<Source>(juce::roundToInt(set(at(ids::source), static_cast<float>(sources[static_cast<size_t>(k)]))));
        l.voices = juce::roundToInt(set(at(ids::voices), 3.0f + shift));
        l.semis = juce::roundToInt(set(at(ids::semis), k == 2 ? -12.0f : 5.0f + shift));
        l.formant = set(at(ids::formant), 2.0f + shift);
        l.levelDb = set(at(ids::level), -8.0f - shift);
        l.width = set(at(ids::layerWidth), 31.0f + shift) * 0.01f;
        l.timeMs = set(at(ids::layerTime), 9.0f + shift);
        l.detuneCents = set(at(ids::detune), 13.0f + shift);
        l.lowHz = set(at(ids::low), 150.0f + 10.0f * shift);
        l.highHz = set(at(ids::high), 9000.0f + 100.0f * shift);
        l.note = juce::roundToInt(set(at(ids::note), 55.0f + shift));
    }
    const auto left = wers::test::loudPhrase(kFs, 2.0, -6.0, 5u), right = wers::test::loudPhrase(kFs, 2.0, -6.0, 9u);
    const int n = static_cast<int>(left.size());
    juce::AudioBuffer<float> played(2, n);
    played.copyFrom(0, 0, left.data(), n);
    played.copyFrom(1, 0, right.data(), n);
    plugin.reset();
    runBlocks(plugin, played, 256);

    RefrenEngine engine;
    engine.prepare(kFs);
    engine.setTempo(0.0);
    engine.setParams(e);
    auto directLeft = left, directRight = right;
    float* channels[] = { directLeft.data(), directRight.data() };
    engine.process(channels, 2, n);
    CHECK(std::memcmp(played.getReadPointer(0), directLeft.data(), sizeof(float) * static_cast<size_t>(n)) == 0);
    CHECK(std::memcmp(played.getReadPointer(1), directRight.data(), sizeof(float) * static_cast<size_t>(n)) == 0);

    // Delta and bypass, which the run above leaves out.
    setParam(plugin, ids::delta, 1.0f);
    auto again = played;
    plugin.reset();
    juce::AudioBuffer<float> wet(2, n);
    wet.copyFrom(0, 0, left.data(), n);
    wet.copyFrom(1, 0, right.data(), n);
    runBlocks(plugin, wet, 256);
    CHECK(std::memcmp(wet.getReadPointer(0), again.getReadPointer(0), sizeof(float) * static_cast<size_t>(n)) != 0);
}

TEST_CASE("values read well in a host") {
    RefrenProcessor p;
    const auto text = [&](const juce::String& id, float plain) {
        auto* parameter = p.state().getParameter(id);
        return parameter->getText(parameter->convertTo0to1(plain), 64);
    };
    const auto pl = [](const char* utf8) { return juce::String::fromUTF8(utf8); };
    CHECK(text(ids::grain, 12.5f) == "12,5 dB");
    CHECK(text(ids::amount, 50.0f) == "50%");
    CHECK(text(ids::tilt, -30.0f) == "-30%");
    CHECK(text(ids::echoTone, 800.0f) == "800 Hz");
    CHECK(text(ids::verbTone, 6000.0f) == "6,00 kHz");
    CHECK(text(ids::motionRate, 1.0f) == "1,00 Hz");
    CHECK(text(ids::verbDecay, 1.5f) == "1,50 s");
    CHECK(text(ids::verbPre, 10.0f) == "10 ms");
    CHECK(text(layerId(0, ids::layerTime), 12.0f) == "12,0 ms");
    CHECK(text(layerId(0, ids::detune), 8.0f) == "8,0 ct");
    CHECK(text(layerId(0, ids::note), 48.0f) == "C3");
    CHECK(text(layerId(0, ids::note), 61.0f) == "C#4");
    CHECK(text(layerId(0, ids::note), 36.0f) == "C2");
    CHECK(text(layerId(0, ids::note), 72.0f) == "C5");
    CHECK(text(layerId(0, ids::semis), 7.0f) == "+7");
    CHECK(text(layerId(0, ids::semis), -12.0f) == "-12");
    CHECK(text(layerId(0, ids::semis), 0.0f) == "0");
    CHECK(text(layerId(0, ids::source), 0.0f) == pl("Głos"));
    CHECK(text(layerId(0, ids::source), 3.0f) == "Robot");
    CHECK(text(ids::grainChar, 1.0f) == pl("Taśma"));
    CHECK(text(ids::motion, 3.0f) == "Flanger");
    CHECK(text(ids::echoTime, 0.0f) == "1/16");
    CHECK(text(ids::echoTime, 2.0f) == pl("1/8 z kropką"));
    CHECK(text(ids::echoTime, 6.0f) == "Slap 80 ms");
    CHECK(text(ids::delta, 1.0f) == pl("wł"));
    CHECK(text(ids::delta, 0.0f) == pl("wył"));
    // Some hosts print the unit after the text, so the text alone carries it.
    for (auto* parameter : p.getParameters()) {
        CAPTURE(parameter->getName(64));
        CHECK(parameter->getLabel().isEmpty());
    }
    const auto back = [&](const juce::String& id, const juce::String& typed) {
        auto* parameter = p.state().getParameter(id);
        return dynamic_cast<juce::RangedAudioParameter*>(parameter)->convertFrom0to1(parameter->getValueForText(typed));
    };
    CHECK(back(ids::grain, "12,5 dB") == doctest::Approx(12.5f));
    CHECK(back(ids::verbTone, "3,5 kHz") == doctest::Approx(3500.0f));
    CHECK(back(layerId(0, ids::note), "c#4") == doctest::Approx(61.0f));
    CHECK(back(layerId(0, ids::semis), "+7") == doctest::Approx(7.0f));
    CHECK(back(ids::delta, pl("wł")) == doctest::Approx(1.0f));
    CHECK(back(ids::delta, pl("wył")) == doctest::Approx(0.0f));
}

TEST_CASE("neutral is a pure delay") {
    for (int channels : { 2, 1 }) {
        CAPTURE(channels);
        RefrenProcessor p;
        p.setPlayConfigDetails(channels, channels, kFs, 256);
        setParam(p, ids::amount, 0.0f);
        setParam(p, layerId(1, ids::on), 1.0f);
        setParam(p, layerId(1, ids::semis), 12.0f);
        setParam(p, ids::grain, 24.0f);
        setParam(p, ids::echoMix, 50.0f);
        setParam(p, ids::verbMix, 50.0f);
        setParam(p, ids::motion, 3.0f);
        setParam(p, ids::motionDepth, 100.0f);
        setParam(p, ids::dry, -12.0f);
        setParam(p, ids::trim, 6.0f);
        p.prepareToPlay(kFs, 256);
        const auto in = noiseBuffer(channels, 48000);
        auto out = in;
        runBlocks(p, out, 256);
        CHECK(20.0 * std::log10(worstError(in, out, latencyFor(kFs)) + 1.0e-30) < -140.0);
    }
}

TEST_CASE("latency is reported") {
    RefrenProcessor p;
    for (double fs : wers::test::kRates) {
        CAPTURE(fs);
        setParam(p, ids::amount, 0.0f);
        p.prepareToPlay(fs, 512);
        CHECK(p.getLatencySamples() == latencyFor(fs));
        CHECK(p.getTailLengthSeconds() == doctest::Approx(latencyFor(fs) / fs + 10.0));
        auto audio = impulseBuffer(static_cast<int>(fs * 0.1));
        runBlocks(p, audio, 512);
        CHECK(peakPlace(audio, 0) == latencyFor(fs));
    }
    p.prepareToPlay(44100.0, 512);
    CHECK(p.getLatencySamples() == latencyFor(44100.0));
}

TEST_CASE("bypass keeps latency without a click") {
    const int n = 96000, change = 256 * 188; // on the edge of a block
    const auto in = sineBuffer(2, n);
    const auto prepared = [] {
        auto p = std::make_unique<RefrenProcessor>();
        p->prepareToPlay(kFs, 256);
        setParam(*p, ids::grain, 18.0f);
        setParam(*p, ids::verbMix, 30.0f);
        setParam(*p, layerId(0, ids::level), 0.0f);
        return p;
    };
    auto steady = in;
    { auto p = prepared(); runBlocks(*p, steady, 256); }
    const double before = worstStep(steady, 0, change, n);

    auto toggled = in;
    {
        auto p = prepared();
        const int latency = p->getLatencySamples();
        juce::MidiBuffer midi;
        for (int from = 0; from < n; from += 256) {
            if (from == change) setParam(*p, ids::bypass, 1.0f);
            juce::AudioBuffer<float> view(toggled.getArrayOfWritePointers(), 2, from, juce::jmin(256, n - from));
            p->processBlock(view, midi);
        }
        CHECK(p->getLatencySamples() == latency);
    }
    CHECK(worstStep(toggled, 0, change, n) <= 2.0 * before);
    // After 10 ms only the delayed input is left.
    const int settled = change + 480;
    bool same = true;
    for (int c = 0; c < 2; ++c)
        for (int i = settled; i < n; ++i) same = same && exactly(toggled.getSample(c, i), in.getSample(c, i - latencyFor(kFs)));
    CHECK(same);
    // Not done sooner, either: the fade takes its 10 ms.
    double stillThere = 0.0;
    for (int i = change; i < change + 256; ++i) stillThere = juce::jmax(stillThere, std::abs(static_cast<double>(toggled.getSample(0, i)) - in.getSample(0, i - latencyFor(kFs))));
    CHECK(stillThere > 0.001);

    // The same when the host calls the bypassed path itself.
    auto third = noiseBuffer(2, 48000);
    const auto thirdIn = third;
    RefrenProcessor p;
    p.prepareToPlay(kFs, 256);
    juce::MidiBuffer midi;
    for (int from = 0; from < third.getNumSamples(); from += 256) {
        juce::AudioBuffer<float> view(third.getArrayOfWritePointers(), 2, from, juce::jmin(256, third.getNumSamples() - from));
        p.processBlockBypassed(view, midi);
    }
    same = true;
    for (int i = 4800; i < 48000; ++i) same = same && exactly(third.getSample(0, i), thirdIn.getSample(0, i - latencyFor(kFs)));
    CHECK(same);
}

TEST_CASE("state round trip and versions") {
    RefrenProcessor a;
    setParam(a, ids::amount, 70.0f);
    setParam(a, ids::tilt, -35.0f);
    setParam(a, ids::grain, 17.0f);
    setParam(a, ids::grainChar, 3.0f);
    setParam(a, ids::motion, 2.0f);
    setParam(a, ids::echoTime, 5.0f);
    setParam(a, ids::echoPing, 1.0f);
    setParam(a, ids::verbSize, 80.0f);
    setParam(a, ids::trim, -4.0f);
    setParam(a, ids::delta, 1.0f);
    setParam(a, layerId(2, ids::on), 1.0f);
    setParam(a, layerId(2, ids::source), 3.0f);
    setParam(a, layerId(2, ids::voices), 9.0f);
    setParam(a, layerId(2, ids::note), 60.0f);
    setParam(a, layerId(3, ids::semis), -12.0f);
    setParam(a, layerId(3, ids::formant), 5.0f);
    a.setUiScale(1.5f);
    a.presets().storeSlot(0);
    setParam(a, ids::grain, 21.0f);
    a.presets().storeSlot(1);
    a.presets().setActiveSlot(1);
    auto named = a.presets().toTree();
    named.setProperty("current", juce::String::fromUTF8("Mój dubel"), nullptr);
    a.presets().fromTree(named);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    const auto* bytes = static_cast<const char*>(saved.getData());
    CHECK(std::memcmp(bytes, "WRFN", 4) == 0);
    CHECK(juce::ByteOrder::littleEndianInt(bytes + 4) == RefrenProcessor::kStateVersion);

    RefrenProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(allValues(a) == allValues(b));
    CHECK(b.uiScale() == doctest::Approx(1.5f));
    CHECK(b.presets().currentName() == juce::String::fromUTF8("Mój dubel"));
    CHECK(b.presets().activeSlot() == 1);
    b.presets().recallSlot(0);
    CHECK(getParam(b, ids::grain) == doctest::Approx(17.0f));
    b.presets().recallSlot(1);
    CHECK(getParam(b, ids::grain) == doctest::Approx(21.0f));

    // A saved project from before a parameter existed gets the default of that parameter.
    const auto xml = juce::parseXML(stateText(a));
    REQUIRE(xml != nullptr);
    for (const char* gone : { ids::tilt, ids::duck }) {
        for (auto* child : xml->getChildIterator())
            if (child->getStringAttribute("id") == gone) { xml->removeChildElement(child, true); break; }
    }
    const auto older = packState(1, xml->toString(juce::XmlElement::TextFormat().withoutHeader().singleLine()));
    RefrenProcessor c;
    setParam(c, ids::tilt, 50.0f);
    setParam(c, ids::duck, 90.0f);
    c.setStateInformation(older.getData(), static_cast<int>(older.getSize()));
    CHECK(getParam(c, ids::tilt) == doctest::Approx(0.0f));
    CHECK(getParam(c, ids::duck) == doctest::Approx(50.0f));
    CHECK(getParam(c, ids::grain) == doctest::Approx(21.0f));

    // Damaged data changes nothing.
    RefrenProcessor d;
    setParam(d, ids::grain, 9.0f);
    const auto before = allValues(d);
    const auto rejected = [&](const void* data, int size) {
        d.setStateInformation(data, size);
        return allValues(d) == before;
    };
    CHECK(rejected(nullptr, 0));
    CHECK(rejected(bytes, 3));
    CHECK(rejected(bytes, 12));
    CHECK(rejected(bytes, static_cast<int>(saved.getSize()) - 20));
    auto wrongMagic = saved;
    static_cast<char*>(wrongMagic.getData())[0] = 'X';
    CHECK(rejected(wrongMagic.getData(), static_cast<int>(wrongMagic.getSize())));
    auto hugeLength = saved;
    writeInt(static_cast<char*>(hugeLength.getData()) + 8, 0x7fffffff);
    CHECK(rejected(hugeLength.getData(), static_cast<int>(hugeLength.getSize())));
    auto noVersion = saved;
    writeInt(static_cast<char*>(noVersion.getData()) + 4, 0);
    CHECK(rejected(noVersion.getData(), static_cast<int>(noVersion.getSize())));
    const auto garbage = packState(1, "<not xml at all");
    CHECK(rejected(garbage.getData(), static_cast<int>(garbage.getSize())));
    const auto otherPlugin = packState(1, "<SOMETHING_ELSE/>");
    CHECK(rejected(otherPlugin.getData(), static_cast<int>(otherPlugin.getSize())));
    juce::Random random(5);
    for (int round = 0; round < 100; ++round) {
        auto scrambled = saved;
        for (int k = 0; k < 8; ++k) static_cast<char*>(scrambled.getData())[random.nextInt(static_cast<int>(scrambled.getSize()))] = static_cast<char>(random.nextInt(256));
        RefrenProcessor e;
        e.setStateInformation(scrambled.getData(), static_cast<int>(scrambled.getSize()));
        auto audio = noiseBuffer(2, 512);
        e.prepareToPlay(kFs, 512);
        runBlocks(e, audio, 512);
        REQUIRE(allFinite(audio));
    }
}

TEST_CASE("host oddities") {
    SUBCASE("a block larger than announced, single samples, silence and nothing") {
        RefrenProcessor p;
        setParam(p, ids::verbMix, 30.0f);
        p.prepareToPlay(96000.0, 64);
        juce::MidiBuffer midi;
        auto big = noiseBuffer(2, 20000);
        p.processBlock(big, midi);
        for (int i = 0; i < 100; ++i) {
            juce::AudioBuffer<float> one(2, 1);
            one.clear();
            p.processBlock(one, midi);
            CHECK(std::isfinite(one.getSample(0, 0)));
        }
        juce::AudioBuffer<float> empty(2, 0);
        p.processBlock(empty, midi);
        juce::AudioBuffer<float> silence(2, 4096);
        silence.clear();
        p.processBlock(silence, midi);
        CHECK(allFinite(silence));
        CHECK(allFinite(big));
    }
    SUBCASE("mono") {
        RefrenProcessor p;
        p.setPlayConfigDetails(1, 1, kFs, 512);
        p.prepareToPlay(kFs, 512);
        setParam(p, ids::amount, 0.0f);
        const auto in = noiseBuffer(1, 48000);
        auto out = in;
        runBlocks(p, out, 512);
        CHECK(worstError(in, out, p.getLatencySamples()) <= 0.0);
    }
    SUBCASE("mono voice on a stereo track") {
        RefrenProcessor p;
        p.setPlayConfigDetails(1, 2, kFs, 512);
        p.prepareToPlay(kFs, 512);
        setParam(p, ids::amount, 0.0f);
        const auto in = noiseBuffer(1, 48000);
        juce::AudioBuffer<float> out(2, 48000);
        out.copyFrom(0, 0, in, 0, 0, 48000);
        juce::Random junk(3);
        for (int i = 0; i < 48000; ++i) out.setSample(1, i, junk.nextFloat()); // what a host may leave here
        runBlocks(p, out, 512);
        CHECK(worstError(in, out, p.getLatencySamples(), 2) <= 0.0);
        // The wet sound spreads to both sides.
        setParam(p, ids::amount, 100.0f);
        juce::AudioBuffer<float> voice(2, 48000);
        const auto phrase = wers::test::loudPhrase(kFs, 1.0, -6.0);
        voice.copyFrom(0, 0, phrase.data(), 48000);
        voice.clear(1, 0, 48000);
        runBlocks(p, voice, 512);
        double difference = 0.0;
        for (int i = 0; i < 48000; ++i) difference += std::abs(voice.getSample(0, i) - voice.getSample(1, i));
        CHECK(difference > 1.0);
    }
    SUBCASE("the sample rate changes") {
        RefrenProcessor p;
        setParam(p, ids::amount, 0.0f);
        for (double fs : { 44100.0, 192000.0, 48000.0 }) {
            CAPTURE(fs);
            p.prepareToPlay(fs, 480);
            CHECK(p.getLatencySamples() == latencyFor(fs));
            const auto in = noiseBuffer(2, 24000);
            auto out = in;
            runBlocks(p, out, 480);
            CHECK(worstError(in, out, latencyFor(fs)) <= 0.0);
        }
    }
    SUBCASE("prepared again and released") {
        RefrenProcessor p;
        p.prepareToPlay(kFs, 256);
        p.releaseResources();
        p.prepareToPlay(kFs, 1024);
        p.reset();
        auto audio = noiseBuffer(2, 2048);
        runBlocks(p, audio, 1024);
        CHECK(allFinite(audio));
    }
    SUBCASE("the tempo disappears and changes") {
        RefrenProcessor p;
        setParam(p, ids::echoMix, 30.0f);
        p.prepareToPlay(kFs, 256);
        FixedTempo head(140.0);
        p.setPlayHead(&head);
        auto audio = noiseBuffer(2, 24000);
        runBlocks(p, audio, 256);
        head.bpm = 0.0;
        auto without = noiseBuffer(2, 24000);
        runBlocks(p, without, 256);
        head.bpm = 75.0;
        auto changed = noiseBuffer(2, 24000);
        runBlocks(p, changed, 256);
        p.setPlayHead(nullptr);
        auto gone = noiseBuffer(2, 24000);
        runBlocks(p, gone, 256);
        CHECK(allFinite(audio));
        CHECK(allFinite(without));
        CHECK(allFinite(changed));
        CHECK(allFinite(gone));
        CHECK(p.getLatencySamples() == latencyFor(kFs));
    }
    SUBCASE("sound that is not a number") {
        RefrenProcessor p;
        p.prepareToPlay(kFs, 256);
        auto audio = noiseBuffer(2, 2560);
        for (int i = 300; i < 400; ++i) audio.setSample(0, i, std::numeric_limits<float>::quiet_NaN());
        for (int i = 700; i < 800; ++i) audio.setSample(1, i, std::numeric_limits<float>::infinity());
        runBlocks(p, audio, 256);
        CHECK(allFinite(audio));
        // Also while bypassed.
        setParam(p, ids::bypass, 1.0f);
        auto bypassed = noiseBuffer(2, 2560);
        for (int i = 300; i < 400; ++i) bypassed.setSample(0, i, std::numeric_limits<float>::quiet_NaN());
        runBlocks(p, bypassed, 256);
        CHECK(allFinite(bypassed));
    }
}

TEST_CASE("tempo comes from the host") {
    const int late = latencyFor(kFs);
    {
        RefrenProcessor p;
        p.prepareToPlay(kFs, 256);
        onlyTheEcho(p);
        FixedTempo head(90.0);
        p.setPlayHead(&head);
        auto audio = impulseBuffer(48000);
        runBlocks(p, audio, 256);
        CHECK(std::abs(peakPlace(audio, 0) - late - 16000) <= 1); // an eighth at 90 is 333 ms
    }
    {
        RefrenProcessor p;
        p.prepareToPlay(kFs, 256);
        onlyTheEcho(p);
        auto audio = impulseBuffer(48000);
        runBlocks(p, audio, 256);
        CHECK(std::abs(peakPlace(audio, 0) - late - 12000) <= 1);
    }
}

TEST_CASE("layouts") {
    RefrenProcessor p;
    const auto layout = [](const juce::AudioChannelSet& in, const juce::AudioChannelSet& out) {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(in);
        l.outputBuses.add(out);
        return l;
    };
    using Set = juce::AudioChannelSet;
    CHECK(p.isBusesLayoutSupported(layout(Set::mono(), Set::mono())));
    CHECK(p.isBusesLayoutSupported(layout(Set::mono(), Set::stereo())));
    CHECK(p.isBusesLayoutSupported(layout(Set::stereo(), Set::stereo())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(Set::stereo(), Set::mono())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(Set::create5point1(), Set::create5point1())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(Set::disabled(), Set::stereo())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(Set::stereo(), Set::disabled())));
}

TEST_CASE("audio thread never allocates") {
    RefrenProcessor p;
    p.prepareToPlay(kFs, 256);
    for (int k = 0; k < kLayers; ++k) setParam(p, layerId(k, ids::on), 1.0f);
    setParam(p, ids::grain, 12.0f);
    setParam(p, ids::echoMix, 30.0f);
    setParam(p, ids::verbMix, 30.0f);
    auto audio = noiseBuffer(2, 48000);
    juce::MidiBuffer midi;
    auto* grain = p.state().getParameter(ids::grain);
    auto* tilt = p.state().getParameter(ids::tilt);
    auto* semis = p.state().getParameter(layerId(1, ids::semis));
    auto* voices = p.state().getParameter(layerId(2, ids::voices));
    auto* size = p.state().getParameter(ids::verbSize);
    auto* motion = p.state().getParameter(ids::motion);
    FixedTempo head(100.0);
    p.setPlayHead(&head);
    p.takeOutputPeak();
    gAllocations = 0;
    gCount = true;
    for (int from = 0; from + 256 <= audio.getNumSamples(); from += 256) {
        const float step = static_cast<float>(from) / static_cast<float>(audio.getNumSamples());
        grain->setValue(step);
        tilt->setValue(1.0f - step);
        semis->setValue(step);
        voices->setValue(step);
        size->setValue(step);
        motion->setValue(step);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, from, 256);
        p.processBlock(view, midi);
    }
    gCount = false;
    p.setPlayHead(nullptr);
    CHECK(gAllocations.load() == 0);
}

TEST_CASE("reset can come from another thread") {
    // The VST3 host calls reset through setProcessing, on a thread of its own, and holds this lock around processBlock.
    RefrenProcessor p;
    p.prepareToPlay(kFs, 256);
    setParam(p, ids::verbMix, 30.0f);
    std::atomic<bool> running{ true };
    std::thread other([&] {
        while (running.load()) {
            p.reset();
            std::this_thread::yield();
        }
    });
    juce::MidiBuffer midi;
    for (int round = 0; round < 200; ++round) {
        auto audio = noiseBuffer(2, 256, 0.1f, round);
        {
            const juce::ScopedLock lock(p.getCallbackLock());
            p.processBlock(audio, midi);
        }
        REQUIRE(allFinite(audio));
    }
    running = false;
    other.join();
}

TEST_CASE("the output peak and the level of the layers are there for the screen") {
    RefrenProcessor p;
    p.prepareToPlay(kFs, 256);
    auto quiet = noiseBuffer(2, 2048, 0.25f);
    runBlocks(p, quiet, 256);
    CHECK(p.takeOutputPeak() < 1.0f);
    CHECK(p.takeOutputPeak() == 0.0f);
    auto loud = noiseBuffer(2, 2048, 4.0f);
    runBlocks(p, loud, 256);
    CHECK(p.takeOutputPeak() > 1.0f);
    CHECK(p.layerLevelNow(0) > 0.0f);
    CHECK(p.layerLevelNow(1) == 0.0f);
    CHECK(p.activeVoices(0) == 2);
    CHECK(p.activeVoices(1) == 0);
    CHECK(p.maskNow() >= 0.0f);
    CHECK(p.maskNow() <= 1.0f);
}

TEST_CASE("editor opens and closes in every state") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    p.prepareToPlay(kFs, 512);
    for (float scale : { 1.0f, 1.5f }) {
        CAPTURE(scale);
        p.setUiScale(scale);
        std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
        auto* editor = dynamic_cast<RefrenEditor*>(window.get());
        REQUIRE(editor != nullptr);
        CHECK(window->getWidth() == juce::roundToInt(1040.0f * scale));
        CHECK(window->getHeight() == juce::roundToInt(680.0f * scale));
        for (bool details : { false, true }) {
            editor->showDetails(details);
            CHECK(editor->detailsShown() == details);
            for (const auto& group : editor->browser().groupNames()) {
                editor->browser().showGroup(group);
                editor->updateNow();
                CHECK(window->createComponentSnapshot(window->getLocalBounds(), true, 1.0f).isValid());
            }
            for (float on : { 1.0f, 0.0f }) {
                setParam(p, ids::delta, on);
                setParam(p, ids::bypass, on);
                auto audio = noiseBuffer(2, 4096, 0.5f);
                runBlocks(p, audio, 512);
                editor->updateNow();
                CHECK(window->createComponentSnapshot(window->getLocalBounds(), true, 1.0f).isValid());
            }
        }
    }
}

TEST_CASE("groups show their presets") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto& browser = dynamic_cast<RefrenEditor&>(*window).browser();
    CHECK(browser.groupNames().size() == static_cast<int>(kGroups.size()) + 2);
    // Each group holds exactly its own presets.
    const auto presets = factoryPresets();
    for (const auto& group : kGroups) {
        const auto name = juce::String::fromUTF8(group.name);
        CAPTURE(name);
        browser.showGroup(name);
        CHECK(browser.shownGroup() == name);
        juce::StringArray wanted;
        for (const auto& preset : presets)
            if (std::strcmp(preset.group, group.name) == 0) wanted.add(juce::String::fromUTF8(preset.name));
        CHECK(browser.shownNames() == wanted);
    }
    // Mine are the user's own.
    const auto mine = juce::String::fromUTF8("Moje");
    browser.showGroup(mine);
    CHECK(browser.shownNames().isEmpty());
    juce::String error;
    const auto own = juce::String::fromUTF8("Mój pierwszy");
    REQUIRE(p.presets().save(own, error));
    browser.showGroup(mine);
    CHECK(browser.shownNames() == juce::StringArray{ own });
    // The favourites are the stars that still exist.
    const auto favourites = juce::String::fromUTF8("Ulubione");
    const auto loose = juce::String::fromUTF8("Luźny");
    browser.showGroup(favourites);
    CHECK(browser.shownNames().isEmpty());
    browser.toggleStar(loose);
    browser.toggleStar(own);
    browser.showGroup(favourites);
    CHECK(browser.shownNames().size() == 2);
    REQUIRE(p.presets().remove(own));
    browser.showGroup(favourites);
    CHECK(browser.shownNames() == juce::StringArray{ loose });
    // A star taken away leaves the list at once.
    browser.toggleStar(loose);
    CHECK(browser.shownNames().isEmpty());
    // Choosing a card loads its preset.
    browser.showGroup(juce::String::fromUTF8(presets[0].group));
    browser.choose(juce::String::fromUTF8(presets[3].name));
    CHECK(p.presets().currentName() == juce::String::fromUTF8(presets[3].name));
}

TEST_CASE("a star stays after reopening") {
    TempFolder folder;
    const auto name = juce::String::fromUTF8("Luźny");
    {
        Favorites favourites(folder.path);
        CHECK(favourites.names().isEmpty());
        CHECK_FALSE(favourites.contains(name));
    }
    {
        RefrenProcessor p(folder.path);
        std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
        auto& browser = dynamic_cast<RefrenEditor&>(*window).browser();
        CHECK_FALSE(browser.hasStar(name));
        browser.toggleStar(name);
        CHECK(browser.hasStar(name));
    }
    {
        RefrenProcessor p(folder.path);
        std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
        auto& browser = dynamic_cast<RefrenEditor&>(*window).browser();
        CHECK(browser.hasStar(name));
        // The file with the stars is no preset.
        CHECK(folder.path.getChildFile("ulubione.txt").existsAsFile());
        CHECK(p.presets().userNames().isEmpty());
        CHECK_FALSE(p.presets().names().contains("ulubione"));
    }
    // A folder that cannot be made costs only the memory of the star.
    const auto blocked = folder.path.getChildFile("to jest plik");
    REQUIRE(blocked.create().wasOk());
    Favorites nowhere(blocked.getChildFile("podfolder"));
    nowhere.toggle(name);
    CHECK(nowhere.contains(name));
    nowhere.toggle(name);
    CHECK_FALSE(nowhere.contains(name));
}

TEST_CASE("a preset that cannot be read says so") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto& editor = dynamic_cast<RefrenEditor&>(*window);
    const auto message = juce::String::fromUTF8(wers::ui::text::presetNoRead);
    const auto file = folder.path.getChildFile(juce::String::fromUTF8("Mój.xml"));
    SUBCASE("a file deleted after the list was made") {
        editor.presetBar().saveAs(juce::String::fromUTF8("Mój"));
        REQUIRE(file.deleteFile());
    }
    SUBCASE("a damaged file") {
        editor.presetBar().saveAs(juce::String::fromUTF8("Mój"));
        REQUIRE(file.replaceWithText("to nie jest preset"));
    }
    editor.browser().choose(juce::String::fromUTF8("Mój"));
    CHECK(editor.presetBar().shownName() == message);
}

TEST_CASE("a star that cannot be written says so") {
    TempFolder folder;
    const auto blocked = folder.path.getChildFile("to jest plik");
    REQUIRE(blocked.create().wasOk());
    RefrenProcessor p(blocked.getChildFile("podfolder"));
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto& editor = dynamic_cast<RefrenEditor&>(*window);
    const auto name = juce::String::fromUTF8("Luźny");
    editor.browser().toggleStar(name);
    CHECK(editor.browser().hasStar(name));
    CHECK(editor.presetBar().shownName() == juce::String::fromUTF8(text::starNoWrite));
}

TEST_CASE("two windows keep each other's stars") {
    TempFolder folder;
    Favorites first(folder.path), second(folder.path);
    REQUIRE(first.toggle("Luzny"));
    REQUIRE(second.toggle("Ciasny"));
    // A click means what the window shows, and the second one shows no star here yet.
    REQUIRE(first.toggle("Refren"));
    REQUIRE(second.toggle("Refren"));
    Favorites later(folder.path);
    CHECK(later.contains("Luzny"));
    CHECK(later.contains("Ciasny"));
    CHECK(later.contains("Refren"));
    // What the other window has done also shows when a group is opened again.
    TempFolder other;
    RefrenProcessor p(other.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto& browser = dynamic_cast<RefrenEditor&>(*window).browser();
    const auto name = juce::String::fromUTF8("Luźny");
    Favorites elsewhere(other.path);
    REQUIRE(elsewhere.toggle(name));
    CHECK_FALSE(browser.hasStar(name));
    browser.showGroup(juce::String::fromUTF8(text::favorites));
    CHECK(browser.hasStar(name));
    CHECK(browser.shownNames().contains(name));
}

TEST_CASE("the star of Losuj does not get in the way") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    wers::ui::PresetBar bar(p.presets());
    juce::Random random(3);
    SUBCASE("on a factory preset") {
        REQUIRE(p.presets().load(juce::String::fromUTF8("Luźny")));
        p.randomise(random);
        REQUIRE(p.presets().currentName().endsWithChar('*'));
        CHECK(bar.nameToOffer().isEmpty());
        CHECK(bar.nameToDelete().isEmpty());
    }
    SUBCASE("on an own preset") {
        const auto mine = juce::String::fromUTF8("Mój");
        bar.saveAs(mine);
        p.randomise(random);
        REQUIRE(p.presets().currentName() == mine + "*");
        CHECK(bar.nameToOffer() == mine);
        CHECK(bar.nameToDelete() == mine);
        REQUIRE(p.presets().remove(bar.nameToDelete()));
        CHECK_FALSE(p.presets().userNames().contains(mine));
        CHECK(p.presets().currentName().isEmpty());
    }
}

TEST_CASE("the big Ruch slider always does something") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto& slider = dynamic_cast<RefrenEditor&>(*window).motionSlider();
    REQUIRE(getParam(p, ids::motion) == doctest::Approx(0.0f));
    REQUIRE(slider.onDragStart != nullptr);
    // Most presets have no movement chosen, so touching the slider picks one.
    slider.onDragStart();
    CHECK(getParam(p, ids::motion) == doctest::Approx(1.0f));
    // A movement that is already there stays.
    setParam(p, ids::motion, 3.0f);
    slider.onDragStart();
    CHECK(getParam(p, ids::motion) == doctest::Approx(3.0f));
}

TEST_CASE("losuj moves only what it may") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    setParam(p, ids::verbMix, 20.0f);
    setParam(p, ids::echoMix, 20.0f);
    setParam(p, ids::tilt, 10.0f);
    juce::StringArray movable{ ids::width, ids::time, ids::tilt, ids::verbMix, ids::echoMix };
    for (int k = 0; k < kLayers; ++k) movable.add(layerId(k, ids::detune));
    const auto before = allValues(p);
    juce::Random random(7);
    p.randomise(random);
    const auto after = allValues(p);
    int moved = 0;
    for (const auto& [id, value] : before) {
        CAPTURE(id);
        if (movable.contains(id)) {
            CHECK(after.at(id) >= 0.0f);
            CHECK(after.at(id) <= 1.0f);
            if (std::abs(after.at(id) - value) > 1.0e-4f) ++moved;
        } else {
            CHECK(after.at(id) == doctest::Approx(value));
        }
    }
    CHECK(moved >= 2);
    const auto name = juce::String::fromUTF8("Luźny");
    CHECK(p.presets().currentName() == name + "*");
    p.randomise(random);
    CHECK(p.presets().currentName() == name + "*");
    // The arrows go on from the preset it came from, and a preset takes the star away.
    wers::ui::PresetBar bar(p.presets());
    bar.setWalkList(juce::StringArray{ juce::String::fromUTF8("Ciasny"), name, juce::String::fromUTF8("Ręczny") });
    bar.step(1);
    CHECK(p.presets().currentName() == juce::String::fromUTF8("Ręczny"));
    p.randomise(random);
    REQUIRE(p.presets().load(name));
    CHECK(p.presets().currentName() == name);
    // Hundreds of presses stay within every range.
    for (int round = 0; round < 300; ++round) p.randomise(random);
    for (const auto& [id, value] : allValues(p)) {
        CAPTURE(id);
        CHECK(std::isfinite(value));
    }
}

TEST_CASE("arrows walk within the group") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    auto& browser = editor->browser();
    juce::String error;
    REQUIRE(p.presets().save("drugi", error));
    REQUIRE(p.presets().save("trzeci", error));
    REQUIRE(p.presets().load(juce::String::fromUTF8("Luźny")));
    browser.showGroup(juce::String::fromUTF8(kGroups[0].name));
    const auto group = browser.shownNames();
    for (int step = 0; step < static_cast<int>(group.size()) * 2 + 3; ++step) {
        editor->presetBar().step(1);
        CHECK(group.contains(p.presets().currentName()));
    }
    browser.showGroup(juce::String::fromUTF8("Moje"));
    const auto mine = browser.shownNames();
    REQUIRE(mine.size() == 2);
    for (int step = 0; step < 5; ++step) {
        editor->presetBar().step(-1);
        CHECK(mine.contains(p.presets().currentName()));
    }
    // An empty group gives the arrows nowhere to go.
    browser.showGroup(juce::String::fromUTF8("Ulubione"));
    const auto where = p.presets().currentName();
    editor->presetBar().step(1);
    CHECK(p.presets().currentName() == where);
    // Without a list the arrows walk over all presets, which is how Barwa has it.
    wers::ui::PresetBar plain(p.presets());
    REQUIRE(p.presets().load(juce::String::fromUTF8("Ciasny")));
    plain.step(1);
    CHECK(p.presets().names().contains(p.presets().currentName()));
    CHECK(p.presets().currentName() != juce::String::fromUTF8("Ciasny"));
}

TEST_CASE("details show every layer") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    CHECK_FALSE(editor->detailsShown());
    CHECK(editor->browser().isVisible());
    editor->showDetails(true);
    CHECK(editor->detailsShown());
    CHECK_FALSE(editor->browser().isVisible());
    const auto shown = editor->idsInDetails();
    juce::StringArray wanted{ ids::grainChar, ids::motion, ids::motionRate, ids::echoTime, ids::echoFeedback, ids::echoPing, ids::echoTone, ids::verbSize, ids::verbDecay, ids::verbTone, ids::verbPre, ids::verbShimmer, ids::duck, ids::sendDry, ids::dry, ids::trim };
    for (int k = 0; k < kLayers; ++k)
        for (const char* suffix : { ids::on, ids::source, ids::voices, ids::semis, ids::formant, ids::level, ids::layerWidth, ids::layerTime, ids::detune, ids::low, ids::high, ids::note }) wanted.add(layerId(k, suffix));
    for (const auto& id : wanted) {
        CAPTURE(id);
        CHECK(shown.contains(id));
    }
    CHECK(shown.size() == wanted.size());
    // What is on the screen moves what it stands for.
    setParam(p, layerId(2, ids::voices), 9.0f);
    editor->updateNow();
    editor->showDetails(false);
    CHECK_FALSE(editor->detailsShown());
    CHECK(editor->browser().isVisible());
}

TEST_CASE("details dim what does nothing") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    auto& column = editor->layerColumn(0);
    const auto dimmed = [&](const char* suffix) { return column.isDimmed(suffix); };
    const auto source = [&](Source s) { setParam(p, layerId(0, ids::source), static_cast<float>(s)); };
    REQUIRE(getParam(p, layerId(0, ids::on)) > 0.5f);
    source(Source::Voice);
    CHECK_FALSE(dimmed(ids::semis));
    CHECK_FALSE(dimmed(ids::formant));
    CHECK(dimmed(ids::note));
    source(Source::Robot);
    CHECK(dimmed(ids::semis));
    CHECK_FALSE(dimmed(ids::formant));
    CHECK_FALSE(dimmed(ids::note));
    source(Source::Sub);
    CHECK_FALSE(dimmed(ids::semis));
    CHECK(dimmed(ids::formant));
    CHECK(dimmed(ids::note));
    source(Source::Whisper);
    CHECK(dimmed(ids::semis));
    CHECK(dimmed(ids::formant));
    CHECK(dimmed(ids::note));
    CHECK_FALSE(dimmed(ids::level));
    // A layer that is off has nothing to say, and it can still be set up.
    setParam(p, layerId(0, ids::on), 0.0f);
    for (const char* suffix : { ids::voices, ids::level, ids::layerWidth, ids::low }) CHECK(dimmed(suffix));
    setParam(p, layerId(0, ids::on), 1.0f);
    CHECK_FALSE(dimmed(ids::level));
    CHECK(editor->layerColumn(1).isDimmed(ids::level));
}

TEST_CASE("a layer switched to sub starts an octave down") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    constexpr int kSub = 2, kVoice = 0;
    auto& second = editor->layerColumn(1);
    CHECK(getParam(p, layerId(1, ids::semis)) == doctest::Approx(0.0f));
    second.pickSource(kSub);
    CHECK(getParam(p, layerId(1, ids::source)) == doctest::Approx(2.0f));
    CHECK(getParam(p, layerId(1, ids::semis)) == doctest::Approx(-12.0f));
    // Another interval stays as it is, also on the way back.
    second.pickSource(kVoice);
    CHECK(getParam(p, layerId(1, ids::semis)) == doctest::Approx(-12.0f));
    auto& third = editor->layerColumn(2);
    setParam(p, layerId(2, ids::semis), 7.0f);
    third.pickSource(kSub);
    CHECK(getParam(p, layerId(2, ids::semis)) == doctest::Approx(7.0f));
    // A preset or a host saying sub with interval 0 is left alone: this is a help for a hand on the screen.
    setParam(p, layerId(3, ids::source), 2.0f);
    editor->updateNow();
    CHECK(getParam(p, layerId(3, ids::semis)) == doctest::Approx(0.0f));
}

TEST_CASE("stage shows every voice") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    p.prepareToPlay(kFs, 512);
    setParam(p, layerId(0, ids::voices), 3.0f);
    setParam(p, layerId(1, ids::on), 1.0f);
    setParam(p, layerId(1, ids::source), 1.0f); // whisper
    setParam(p, layerId(1, ids::voices), 5.0f);
    setParam(p, layerId(1, ids::level), -6.0f);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    auto& stage = editor->stage();
    const auto play = [&](const std::vector<float>& x, int from, int to) {
        juce::AudioBuffer<float> audio(2, to - from);
        audio.copyFrom(0, 0, x.data() + from, to - from);
        audio.copyFrom(1, 0, x.data() + from, to - from);
        runBlocks(p, audio, 512);
        editor->updateNow();
    };
    const auto vowel = wers::test::vowel(150.0, -12.0, 48000, kFs);
    play(vowel, 0, 48000);
    CHECK(stage.voiceDots() == 8);
    CHECK(stage.dotCount() == 9); // and the dry voice
    const float voiceOnVowel = stage.brightnessOf(0), whisperOnVowel = stage.brightnessOf(1);
    MESSAGE("on a vowel: voice " << voiceOnVowel << ", whisper " << whisperOnVowel);
    CHECK(voiceOnVowel > 0.3f);
    CHECK(whisperOnVowel > 0.3f);
    const auto hiss = wers::test::bandNoise(4000.0, 9000.0, -12.0, 12000, kFs);
    play(hiss, 0, 12000);
    MESSAGE("on a sibilant: voice " << stage.brightnessOf(0) << ", whisper " << stage.brightnessOf(1));
    CHECK(stage.brightnessOf(0) < 0.5f * voiceOnVowel);
    CHECK(stage.brightnessOf(1) > 0.5f * whisperOnVowel);
    // Voices that are turned off leave the stage.
    setParam(p, layerId(1, ids::on), 0.0f);
    play(vowel, 0, 4800);
    CHECK(stage.voiceDots() == 3);
}

TEST_CASE("a typed preset name reaches the store") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    const auto name = juce::String::fromUTF8("Mój dubel żółty");
    editor->presetBar().saveAs(name);
    CHECK(p.presets().currentName() == name);
    CHECK(p.presets().userNames().contains(name));
    CHECK(folder.path.getChildFile(name + ".xml").existsAsFile());
    editor->updateNow();
    CHECK(editor->presetBar().shownName() == name);
    editor->browser().showGroup(juce::String::fromUTF8("Moje"));
    CHECK(editor->browser().shownNames().contains(name));
}

TEST_CASE("the screen rests when no sound comes") {
    TempFolder folder;
    RefrenProcessor p(folder.path);
    p.prepareToPlay(kFs, 512);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<RefrenEditor*>(window.get());
    auto audio = noiseBuffer(2, 8192, 0.5f);
    runBlocks(p, audio, 512);
    editor->updateNow();
    const int moved = editor->stage().moves();
    CHECK(moved > 0);
    for (int tick = 0; tick < 200; ++tick) editor->updateNow();
    // A few more steps while the last sound fades, then it stops.
    const int settled = editor->stage().moves();
    CHECK(settled <= moved + 30);
    for (int tick = 0; tick < 100; ++tick) editor->updateNow();
    CHECK(editor->stage().moves() == settled);
}

TEST_CASE("the measurement file holds only numbers") {
    namespace fs = std::filesystem;
    namespace wav = wers::wav;
    const auto folder = fs::temp_directory_path() / "refren_measure_test";
    fs::create_directories(folder);
    const auto s = wers::test::makeSyllables(kFs, 3);
    wav::Audio audio;
    audio.rate = static_cast<int>(kFs);
    audio.channels = 2;
    audio.ch = { s.signal, s.signal };
    const std::string name = "Żółć numer sekretny";
    const auto first = folder / fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()) + u8".wav");
    {
        // Written through the path object, so the name with Polish letters also works on Windows.
        const auto temporary = folder / "temporary.wav";
        REQUIRE(wav::write(temporary.string(), audio));
        fs::rename(temporary, first);
    }
    const auto list = folder / "list.txt";
    const auto out = folder / "out.tsv";
    {
        std::ofstream file(list, std::ios::binary);
        const auto bytes = first.u8string();
        file << "\xEF\xBB\xBF" << std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) << "\r\n";
        file << (folder / "nie ma takiego pliku.wav").string() << "\n\n";
        file << "\xFF\xFE to nie jest tekst.wav\n";
    }
    REQUIRE(measureVoiceList(list.u8string(), out.u8string()) == 1);
    std::string text;
    {
        // Closed before the folder goes: Windows does not delete a file that is still open.
        std::ifstream result(out, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(result), std::istreambuf_iterator<char>());
    }
    CAPTURE(text);
    CHECK(text.rfind("index\tseconds\tvoicedShare\tpitchSpreadCents\tsibSideDropDb\tlevelDiffDb\n", 0) == 0);
    CHECK(text.find("sekretny") == std::string::npos);
    CHECK(text.find("wav") == std::string::npos);
    CHECK(text.find(":") == std::string::npos);
    CHECK(text.find("/") == std::string::npos);
    CHECK(text.find("\\") == std::string::npos);
    // One row a file in the list: a file that could not be read says so with zero seconds.
    CHECK(std::count(text.begin(), text.end(), '\n') == 4);
    const auto firstRow = text.find('\n') + 1;
    double index = 0.0, seconds = 0.0, voiced = 0.0, spread = 0.0, drop = 0.0, level = 0.0;
    REQUIRE(std::sscanf(text.c_str() + firstRow, "%lf %lf %lf %lf %lf %lf", &index, &seconds, &voiced, &spread, &drop, &level) == 6);
    MESSAGE("voiced " << voiced << ", spread " << spread << " cents, sibilants on the sides down " << drop << " dB, level " << level << " dB");
    CHECK(index == 1.0);
    CHECK(seconds == doctest::Approx(static_cast<double>(s.signal.size()) / kFs).epsilon(0.01));
    CHECK(voiced > 0.3);
    CHECK(drop > 6.0);
    CHECK(std::abs(level) < 12.0);
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
