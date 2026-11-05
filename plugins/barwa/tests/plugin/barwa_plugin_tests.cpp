// Tests of the whole plugin through the same entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "PresetBar.h"
#include "PresetStore.h"
#include "SharedText.h"
#include "TestSignals.h"
#include "plugin/BarwaProcessor.h"
#include "plugin/FactoryPresets.h"
#include "ui/BarwaEditor.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <thread>

using namespace barwa;

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

void setParam(BarwaProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(BarwaProcessor& p, const char* id) { return p.state().getRawParameterValue(id)->load(); }

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

juce::AudioBuffer<float> noiseBuffer(int channels, int n, float gain = 0.1f, int seed = 1) {
    juce::AudioBuffer<float> b(channels, n);
    juce::Random r(seed);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i) b.setSample(c, i, (r.nextFloat() * 2.0f - 1.0f) * gain);
    return b;
}

void runBlocks(BarwaProcessor& p, juce::AudioBuffer<float>& audio, int block) {
    juce::MidiBuffer midi;
    for (int from = 0; from < audio.getNumSamples(); from += block) {
        const int n = juce::jmin(block, audio.getNumSamples() - from);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), from, n);
        p.processBlock(view, midi);
    }
}

double worstError(const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency) {
    double worst = 0.0;
    for (int c = 0; c < in.getNumChannels(); ++c)
        for (int i = latency; i < in.getNumSamples(); ++i)
            worst = juce::jmax(worst, std::abs(static_cast<double>(out.getSample(c, i)) - in.getSample(c, i - latency)));
    return worst;
}

std::map<juce::String, float> allValues(BarwaProcessor& p) {
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
    out.write("WBRW", 4);
    out.writeInt(version);
    const auto text = xml.toRawUTF8();
    const auto length = xml.getNumBytesAsUTF8();
    out.writeInt(static_cast<int>(length));
    out.write(text, length);
    out.flush();
    return block;
}

juce::String stateText(BarwaProcessor& p) {
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    const auto length = static_cast<size_t>(juce::ByteOrder::littleEndianInt(static_cast<const char*>(saved.getData()) + 8));
    return juce::String::fromUTF8(static_cast<const char*>(saved.getData()) + 12, static_cast<int>(length));
}

juce::AudioProcessor::TrackProperties named(const juce::String& name) {
    juce::AudioProcessor::TrackProperties properties;
    properties.name = name;
    return properties;
}

} // namespace

TEST_CASE("neutral settings are a pure delay") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    const int latency = p.getLatencySamples();
    CHECK(latency == 192 + 57);
    CHECK(p.getTailLengthSeconds() == doctest::Approx(latency / 48000.0));
    const auto in = noiseBuffer(2, 48000);
    auto out = in;
    runBlocks(p, out, 256);
    CHECK(worstError(in, out, latency) == 0.0);
}

TEST_CASE("bypass keeps latency without a click") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, ids::drive1, 18.0f);
    const int latency = p.getLatencySamples();
    const auto in = noiseBuffer(2, 48000);
    auto first = in;
    runBlocks(p, first, 256);
    setParam(p, ids::bypass, 1.0f);
    auto second = in;
    runBlocks(p, second, 256);
    CHECK(p.getLatencySamples() == latency);
    bool same = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 4800 + latency; i < 48000; ++i) same = same && second.getSample(c, i) == in.getSample(c, i - latency);
    CHECK(same);
    float worst = 0.0f;
    for (int i = 1; i < 4800; ++i) worst = juce::jmax(worst, std::abs(second.getSample(0, i) - second.getSample(0, i - 1)));
    CHECK(worst < 0.45f);

    // The same when the host calls the bypassed path itself.
    juce::MidiBuffer midi;
    auto third = in;
    for (int from = 0; from < third.getNumSamples(); from += 256) {
        juce::AudioBuffer<float> view(third.getArrayOfWritePointers(), 2, from, juce::jmin(256, third.getNumSamples() - from));
        p.processBlockBypassed(view, midi);
    }
    same = true;
    for (int i = 4800 + latency; i < 48000; ++i) same = same && third.getSample(0, i) == in.getSample(0, i - latency);
    CHECK(same);
}

TEST_CASE("audio thread never allocates") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, ids::bands, 2.0f);
    setParam(p, ids::drive1, 12.0f);
    setParam(p, ids::drive2, 12.0f);
    setParam(p, ids::drive3, 12.0f);
    auto audio = noiseBuffer(2, 48000);
    juce::MidiBuffer midi;
    float scopeIn[256], scopeOut[256];
    auto* drive = p.state().getParameter(ids::drive1);
    auto* tone = p.state().getParameter(ids::tone);
    auto* bands = p.state().getParameter(ids::bands);
    auto* character = p.state().getParameter(ids::char1);
    p.takeOutputPeak();
    gAllocations = 0;
    gCount = true;
    for (int from = 0; from + 256 <= audio.getNumSamples(); from += 256) {
        const float step = static_cast<float>(from) / static_cast<float>(audio.getNumSamples());
        drive->setValue(step);
        tone->setValue(1.0f - step);
        bands->setValue(step);
        character->setValue(step);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, from, 256);
        p.processBlock(view, midi);
    }
    gCount = false;
    CHECK(gAllocations.load() == 0);
    CHECK(p.popScope(scopeIn, scopeOut, 256));
}

TEST_CASE("state round trip and versions") {
    BarwaProcessor a;
    setParam(a, ids::bands, 3.0f);
    setParam(a, ids::xover1, 300.0f);
    setParam(a, ids::xover2, 4000.0f);
    setParam(a, ids::char2, 3.0f);
    setParam(a, ids::drive3, 17.0f);
    setParam(a, ids::bandMix1, 40.0f);
    setParam(a, ids::solo2, 1.0f);
    setParam(a, ids::mask, 60.0f);
    setParam(a, ids::tone, -35.0f);
    setParam(a, ids::mix, 80.0f);
    setParam(a, ids::autoLevel, 0.0f);
    setParam(a, ids::output, -3.0f);
    setParam(a, ids::delta, 1.0f);
    setParam(a, ids::quality, 2.0f);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    const auto* bytes = static_cast<const char*>(saved.getData());
    CHECK(std::memcmp(bytes, "WBRW", 4) == 0);
    CHECK(juce::ByteOrder::littleEndianInt(bytes + 4) == BarwaProcessor::kStateVersion);
    CHECK(BarwaProcessor::kStateVersion == 1);

    BarwaProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(allValues(a) == allValues(b));
    pump(50);
    CHECK(b.getLatencySamples() == a.getLatencySamples());

    // A saved project from before a parameter existed gets the neutral value of that parameter.
    const auto xml = juce::parseXML(stateText(a));
    REQUIRE(xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute("id") == ids::tone) { xml->removeChildElement(child, true); break; }
    const auto older = packState(1, xml->toString(juce::XmlElement::TextFormat().withoutHeader().singleLine()));
    BarwaProcessor c;
    setParam(c, ids::tone, 50.0f);
    c.setStateInformation(older.getData(), static_cast<int>(older.getSize()));
    CHECK(getParam(c, ids::tone) == doctest::Approx(0.0f));
    CHECK(getParam(c, ids::drive3) == doctest::Approx(17.0f));

    // Damaged data changes nothing.
    BarwaProcessor d;
    setParam(d, ids::drive1, 9.0f);
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
    for (int round = 0; round < 200; ++round) {
        auto scrambled = saved;
        for (int k = 0; k < 8; ++k) static_cast<char*>(scrambled.getData())[random.nextInt(static_cast<int>(scrambled.getSize()))] = static_cast<char>(random.nextInt(256));
        BarwaProcessor e;
        e.setStateInformation(scrambled.getData(), static_cast<int>(scrambled.getSize()));
        auto audio = noiseBuffer(2, 512);
        e.prepareToPlay(48000.0, 512);
        runBlocks(e, audio, 512);
        for (int i = 0; i < 512; ++i) REQUIRE(std::isfinite(audio.getSample(0, i)));
    }
}

TEST_CASE("track name sets the mask once") {
    for (const char* name : { "beat", "bit", "instrumental", "instr", "prod", "master", "mix", "Main Beat 2", "my_mix" }) {
        CAPTURE(juce::String(name));
        BarwaProcessor p;
        CHECK(getParam(p, ids::mask) == doctest::Approx(100.0f));
        p.updateTrackProperties(named(name));
        CHECK(getParam(p, ids::mask) == doctest::Approx(0.0f));
    }
    for (const char* name : { "orbit", "vocal", "Track 3", "habit", "remix" }) {
        CAPTURE(juce::String(name));
        BarwaProcessor p;
        p.updateTrackProperties(named(name));
        CHECK(getParam(p, ids::mask) == doctest::Approx(100.0f));
    }

    BarwaProcessor moved;
    setParam(moved, ids::mask, 70.0f);
    moved.updateTrackProperties(named("beat"));
    CHECK(getParam(moved, ids::mask) == doctest::Approx(70.0f));

    BarwaProcessor renamed;
    renamed.updateTrackProperties(named("beat"));
    setParam(renamed, ids::mask, 30.0f);
    renamed.updateTrackProperties(named("master"));
    CHECK(getParam(renamed, ids::mask) == doctest::Approx(30.0f));

    // A project remembers what it had, even when its value is the default one.
    BarwaProcessor saved;
    juce::MemoryBlock block;
    saved.getStateInformation(block);
    BarwaProcessor loaded;
    loaded.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    loaded.updateTrackProperties(named("beat"));
    CHECK(getParam(loaded, ids::mask) == doctest::Approx(100.0f));
}

TEST_CASE("changing quality while playing") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, ids::drive1, 12.0f);
    std::atomic<bool> running{ true };
    std::atomic<float> peak{ 0.0f };
    std::atomic<bool> finite{ true };
    std::atomic<int> blocks{ 0 };
    const float inputPeak = 0.5f;
    std::thread audio([&] {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0;
        while (running.load()) {
            for (int i = 0; i < 256; ++i) {
                const float v = inputPeak * static_cast<float>(std::sin(phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0;
                buffer.setSample(0, i, v);
                buffer.setSample(1, i, v);
            }
            {
                const juce::ScopedLock lock(p.getCallbackLock());
                if (p.isSuspended()) buffer.clear();
                else p.processBlock(buffer, midi);
            }
            for (int i = 0; i < 256; ++i) {
                const float v = std::abs(buffer.getSample(0, i));
                if (!std::isfinite(v)) finite = false;
                peak = juce::jmax(peak.load(), v);
            }
            blocks.fetch_add(1);
            std::this_thread::yield();
        }
    });
    const std::array<int, 3> added{ 49, 57, 59 };
    for (int round = 0; round < 50; ++round) {
        const int quality = round % 3;
        setParam(p, ids::quality, static_cast<float>(quality));
        pump(30);
        CHECK(p.getLatencySamples() == 192 + added[static_cast<size_t>(quality)]);
    }
    running = false;
    audio.join();
    CHECK(blocks.load() > 0);
    CHECK(finite.load());
    MESSAGE("peak " << juce::Decibels::gainToDecibels(peak.load() / inputPeak) << " dB over " << blocks.load() << " blocks");
    CHECK(peak.load() <= inputPeak * juce::Decibels::decibelsToGain(1.0f));
}

TEST_CASE("host oddities") {
    SUBCASE("a block larger than announced, single samples, silence and nothing") {
        BarwaProcessor p;
        setParam(p, ids::drive1, 12.0f);
        p.prepareToPlay(96000.0, 64);
        juce::MidiBuffer midi;
        auto big = noiseBuffer(2, 20000);
        p.processBlock(big, midi);
        for (int i = 0; i < 100; ++i) {
            juce::AudioBuffer<float> one(2, 1);
            one.clear();
            p.processBlock(one, midi);
            CHECK(one.getSample(0, 0) == one.getSample(0, 0));
        }
        juce::AudioBuffer<float> empty(2, 0);
        p.processBlock(empty, midi);
        juce::AudioBuffer<float> silence(2, 4096);
        silence.clear();
        p.processBlock(silence, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4096; ++i) REQUIRE(std::isfinite(silence.getSample(c, i)));
        for (int i = 0; i < 20000; ++i) REQUIRE(std::isfinite(big.getSample(0, i)));
    }
    SUBCASE("mono") {
        BarwaProcessor p;
        p.setPlayConfigDetails(1, 1, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        const auto in = noiseBuffer(1, 48000);
        auto out = in;
        runBlocks(p, out, 512);
        CHECK(worstError(in, out, p.getLatencySamples()) == 0.0);
    }
    SUBCASE("the sample rate changes") {
        BarwaProcessor p;
        for (double fs : { 44100.0, 192000.0, 48000.0 }) {
            CAPTURE(fs);
            p.prepareToPlay(fs, 480);
            const int expected = juce::roundToInt(fs * 0.004) + (fs > 100000.0 ? 49 : 57);
            CHECK(p.getLatencySamples() == expected);
            const auto in = noiseBuffer(2, 24000);
            auto out = in;
            runBlocks(p, out, 480);
            CHECK(worstError(in, out, expected) == 0.0);
        }
    }
    SUBCASE("prepared again and released") {
        BarwaProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.releaseResources();
        p.prepareToPlay(48000.0, 1024);
        p.reset();
        auto audio = noiseBuffer(2, 2048);
        runBlocks(p, audio, 1024);
        for (int i = 0; i < 2048; ++i) REQUIRE(std::isfinite(audio.getSample(0, i)));
    }
    SUBCASE("sound that is not a number") {
        BarwaProcessor p;
        p.prepareToPlay(48000.0, 256);
        auto audio = noiseBuffer(2, 2560);
        for (int i = 300; i < 400; ++i) audio.setSample(0, i, std::numeric_limits<float>::quiet_NaN());
        for (int i = 700; i < 800; ++i) audio.setSample(1, i, std::numeric_limits<float>::infinity());
        runBlocks(p, audio, 256);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 2560; ++i) REQUIRE(std::isfinite(audio.getSample(c, i)));
        // Also while bypassed.
        setParam(p, ids::bypass, 1.0f);
        auto bypassed = noiseBuffer(2, 2560);
        for (int i = 300; i < 400; ++i) bypassed.setSample(0, i, std::numeric_limits<float>::quiet_NaN());
        runBlocks(p, bypassed, 256);
        for (int i = 0; i < 2560; ++i) REQUIRE(std::isfinite(bypassed.getSample(0, i)));
    }
}

TEST_CASE("layouts") {
    BarwaProcessor p;
    const auto layout = [](const juce::AudioChannelSet& in, const juce::AudioChannelSet& out) {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(in);
        l.outputBuses.add(out);
        return l;
    };
    CHECK(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
    CHECK(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
    CHECK_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::disabled(), juce::AudioChannelSet::stereo())));
}

TEST_CASE("parameters match the specification") {
    BarwaProcessor p;
    const auto check = [&](const char* id, float low, float high, float initial, bool automatable) {
        CAPTURE(juce::String(id));
        auto* param = p.state().getParameter(id);
        REQUIRE(param != nullptr);
        auto range = p.state().getParameterRange(id);
        CHECK(range.start == doctest::Approx(low));
        CHECK(range.end == doctest::Approx(high));
        CHECK(range.convertFrom0to1(param->getDefaultValue()) == doctest::Approx(initial));
        CHECK(param->isAutomatable() == automatable);
    };
    check(ids::bands, 0.0f, 2.0f, 0.0f, false);
    check(ids::xover1, 60.0f, 1000.0f, 200.0f, true);
    check(ids::xover2, 1000.0f, 10000.0f, 3000.0f, true);
    for (const char* id : { ids::char1, ids::char2, ids::char3 }) check(id, 0.0f, 3.0f, 0.0f, false);
    for (const char* id : { ids::drive1, ids::drive2, ids::drive3 }) check(id, 0.0f, 36.0f, 0.0f, true);
    for (const char* id : { ids::bandMix1, ids::bandMix2, ids::bandMix3 }) check(id, 0.0f, 100.0f, 100.0f, true);
    for (const char* id : { ids::solo1, ids::solo2, ids::solo3 }) check(id, 0.0f, 1.0f, 0.0f, false);
    check(ids::mask, 0.0f, 100.0f, 100.0f, true);
    check(ids::tone, -100.0f, 100.0f, 0.0f, true);
    check(ids::mix, 0.0f, 100.0f, 100.0f, true);
    check(ids::autoLevel, 0.0f, 1.0f, 1.0f, false);
    check(ids::output, -24.0f, 24.0f, 0.0f, true);
    check(ids::delta, 0.0f, 1.0f, 0.0f, false);
    check(ids::quality, 0.0f, 2.0f, 1.0f, false);
    check(ids::bypass, 0.0f, 1.0f, 0.0f, true);
    CHECK(p.getParameters().size() == 23);
    CHECK(p.getBypassParameter() == p.state().getParameter(ids::bypass));
}

TEST_CASE("the output peak is there for the warning") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    auto quiet = noiseBuffer(2, 2048, 0.25f);
    runBlocks(p, quiet, 256);
    CHECK(p.takeOutputPeak() < 1.0f);
    auto loud = noiseBuffer(2, 2048, 2.0f);
    runBlocks(p, loud, 256);
    auto tail = noiseBuffer(2, 512, 2.0f);
    runBlocks(p, tail, 256);
    CHECK(p.takeOutputPeak() > 1.0f);
    CHECK(p.takeOutputPeak() == 0.0f);
}

TEST_CASE("values read well in a host") {
    BarwaProcessor p;
    const auto text = [&](const char* id, float plain) {
        auto* parameter = p.state().getParameter(id);
        return parameter->getText(parameter->convertTo0to1(plain), 64);
    };
    CHECK(text(ids::drive1, 12.5f) == "12,5 dB");
    CHECK(text(ids::mask, 50.0f) == "50%");
    CHECK(text(ids::xover1, 200.0f) == "200 Hz");
    CHECK(text(ids::xover2, 3000.0f) == "3,00 kHz");
    // Some hosts print the unit after the text, so the text alone carries it.
    for (auto* parameter : p.getParameters()) {
        CAPTURE(parameter->getName(64));
        CHECK(parameter->getLabel().isEmpty());
    }
    auto* drive = p.state().getParameter(ids::drive1);
    CHECK(drive->convertFrom0to1(drive->getValueForText("12,5 dB")) == doctest::Approx(12.5f));
    auto* split = p.state().getParameter(ids::xover2);
    CHECK(split->convertFrom0to1(split->getValueForText("3,5 kHz")) == doctest::Approx(3500.0f));
}

TEST_CASE("reset can come from another thread while quality changes") {
    // The VST3 host calls reset through setProcessing, on a thread of its own.
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    std::atomic<bool> running{ true };
    std::thread other([&] {
        while (running.load()) {
            p.reset();
            std::this_thread::yield();
        }
    });
    for (int round = 0; round < 30; ++round) {
        setParam(p, ids::quality, static_cast<float>(round % 3));
        pump(10);
    }
    running = false;
    other.join();
    auto audio = noiseBuffer(2, 2048);
    runBlocks(p, audio, 256);
    for (int i = 0; i < 2048; ++i) REQUIRE(std::isfinite(audio.getSample(0, i)));
}

TEST_CASE("the screen rests when no sound comes") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<BarwaEditor*>(window.get());
    REQUIRE(editor != nullptr);
    auto audio = noiseBuffer(2, 8192, 0.5f);
    runBlocks(p, audio, 512);
    editor->updateNow();
    const float loud = editor->spectrum().loudestShownDb();
    CHECK(loud > -60.0f);
    const int moved = editor->maskStrip().moves();
    for (int tick = 0; tick < 200; ++tick) editor->updateNow();
    CHECK(editor->spectrum().loudestShownDb() < loud - 20.0f);
    // A few more steps while the last sound fades, then it stops.
    CHECK(editor->maskStrip().moves() <= moved + 15);
}

TEST_CASE("the mask value reaches the sound") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, ids::drive1, 12.0f);
    setParam(p, ids::autoLevel, 0.0f);
    setParam(p, ids::delta, 1.0f);
    // Hiss alone: with the mask fully on, almost nothing is added.
    auto hiss = noiseBuffer(2, 48000, 0.2f);
    runBlocks(p, hiss, 256);
    const float masked = hiss.getRMSLevel(0, 24000, 24000);
    setParam(p, ids::mask, 0.0f);
    auto again = noiseBuffer(2, 48000, 0.2f);
    runBlocks(p, again, 256);
    const float open = again.getRMSLevel(0, 24000, 24000);
    CHECK(open > masked * 4.0f);
    CHECK(p.maskNow() == doctest::Approx(1.0f));
}

namespace {

juce::MouseEvent mouseAt(juce::Component& component, float x, float y) {
    const juce::Point<float> position(x, y);
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, position, now, 1, false);
}

class GestureCounter final : public juce::AudioProcessorParameter::Listener {
public:
    int begun = 0, ended = 0, changes = 0;
    void parameterValueChanged(int, float) override { ++changes; }
    void parameterGestureChanged(int, bool starting) override { ++(starting ? begun : ended); }
};

} // namespace

TEST_CASE("editor opens and closes in every state") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 512);
    juce::MidiBuffer midi;
    for (int round = 0; round < 3; ++round) {
        for (float bands : { 0.0f, 1.0f, 2.0f }) {
            setParam(p, ids::bands, bands);
            for (bool withSound : { false, true }) {
                std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
                auto* editor = dynamic_cast<BarwaEditor*>(window.get());
                REQUIRE(editor != nullptr);
                CHECK(window->getWidth() == 1040);
                CHECK(window->getHeight() == 680);
                if (withSound) {
                    auto audio = noiseBuffer(2, 4096, 0.5f);
                    runBlocks(p, audio, 512);
                }
                editor->updateNow();
                const auto image = window->createComponentSnapshot(window->getLocalBounds(), true, 1.0f);
                CHECK(image.isValid());
            }
        }
    }
    // It keeps the size it was left at, within limits.
    p.setUiScale(1.3f);
    std::unique_ptr<juce::AudioProcessorEditor> larger(p.createEditor());
    CHECK(larger->getWidth() == juce::roundToInt(1040 * 1.3f));
    CHECK(larger->getHeight() == juce::roundToInt(680 * 1.3f));
}

TEST_CASE("dragging a split changes its parameter") {
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<BarwaEditor*>(window.get());
    REQUIRE(editor != nullptr);
    auto& view = editor->spectrum();

    // With one band there is nothing to drag.
    editor->updateNow();
    CHECK(view.handleCount() == 0);
    const float before = getParam(p, ids::xover1);
    view.mouseDown(mouseAt(view, 300.0f, 100.0f));
    view.mouseDrag(mouseAt(view, 400.0f, 100.0f));
    view.mouseUp(mouseAt(view, 400.0f, 100.0f));
    CHECK(getParam(p, ids::xover1) == doctest::Approx(before));

    setParam(p, ids::bands, 2.0f);
    editor->updateNow();
    REQUIRE(view.handleCount() == 2);

    GestureCounter counter;
    auto* parameter = p.state().getParameter(ids::xover1);
    parameter->addListener(&counter);
    const float x = view.handleX(0);
    view.mouseDown(mouseAt(view, x, 100.0f));
    CHECK(counter.begun == 1);
    view.mouseDrag(mouseAt(view, x + 60.0f, 100.0f));
    CHECK(getParam(p, ids::xover1) > before * 1.2f);
    CHECK(counter.changes > 0);
    view.mouseUp(mouseAt(view, x + 60.0f, 100.0f));
    CHECK(counter.ended == 1);
    parameter->removeListener(&counter);

    // The second handle moves the second split, and a split stops at the end of its range.
    const float second = view.handleX(1);
    const float xover1Now = getParam(p, ids::xover1);
    view.mouseDown(mouseAt(view, second, 100.0f));
    view.mouseDrag(mouseAt(view, 1030.0f, 100.0f));
    view.mouseUp(mouseAt(view, 1030.0f, 100.0f));
    CHECK(getParam(p, ids::xover2) == doctest::Approx(10000.0f));
    CHECK(getParam(p, ids::xover1) == doctest::Approx(xover1Now));

    // A click away from the handles changes nothing.
    const float xover2Now = getParam(p, ids::xover2);
    view.mouseDown(mouseAt(view, 5.0f, 100.0f));
    view.mouseDrag(mouseAt(view, 50.0f, 100.0f));
    view.mouseUp(mouseAt(view, 50.0f, 100.0f));
    CHECK(getParam(p, ids::xover1) == doctest::Approx(xover1Now));
    CHECK(getParam(p, ids::xover2) == doctest::Approx(xover2Now));
}

TEST_CASE("band columns follow the band count") {
    BarwaProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<BarwaEditor*>(window.get());
    REQUIRE(editor != nullptr);
    for (int bands : { 1, 3, 2, 1 }) {
        setParam(p, ids::bands, static_cast<float>(bands - 1));
        editor->updateNow();
        CHECK(editor->visibleBandColumns() == bands);
        CHECK(editor->spectrum().handleCount() == bands - 1);
    }
}

namespace {

const juce::StringArray kExcluded{ ids::quality, ids::bypass, ids::delta, ids::solo1, ids::solo2, ids::solo3 };

// A fresh folder for one test, gone at the end of it.
struct TempFolder {
    juce::TemporaryFile holder;
    juce::File path = holder.getFile();
    TempFolder() { path.createDirectory(); }
    ~TempFolder() { path.deleteRecursively(); }
};

void changeEverything(BarwaProcessor& p) {
    setParam(p, ids::bands, 2.0f);
    setParam(p, ids::xover1, 321.0f);
    setParam(p, ids::xover2, 4567.0f);
    setParam(p, ids::char1, 1.0f);
    setParam(p, ids::char2, 2.0f);
    setParam(p, ids::char3, 3.0f);
    setParam(p, ids::drive1, 5.5f);
    setParam(p, ids::drive2, 6.5f);
    setParam(p, ids::drive3, 7.5f);
    setParam(p, ids::bandMix1, 11.0f);
    setParam(p, ids::bandMix2, 22.0f);
    setParam(p, ids::bandMix3, 33.0f);
    setParam(p, ids::mask, 44.0f);
    setParam(p, ids::tone, -55.0f);
    setParam(p, ids::mix, 66.0f);
    setParam(p, ids::autoLevel, 0.0f);
    setParam(p, ids::output, -7.0f);
}

std::map<juce::String, float> presetValues(BarwaProcessor& p) {
    auto values = allValues(p);
    for (const auto& id : kExcluded) values.erase(id);
    return values;
}

} // namespace

TEST_CASE("preset saves and loads exactly") {
    TempFolder folder;
    BarwaProcessor p;
    p.prepareToPlay(48000.0, 256);
    wers::ui::PresetStore store(p.state(), folder.path, factoryPresets(), kExcluded);
    changeEverything(p);
    const auto wanted = presetValues(p);
    juce::String error;
    REQUIRE(store.save("moje", error));
    CHECK(error.isEmpty());
    CHECK(store.names().contains("moje"));
    CHECK(folder.path.getChildFile("moje.xml").existsAsFile());
    const auto root = juce::parseXML(folder.path.getChildFile("moje.xml"));
    REQUIRE(root != nullptr);
    CHECK(root->hasTagName("WERS_PRESET"));
    CHECK(root->getStringAttribute("plugin") == "BARWA");
    CHECK(root->getIntAttribute("version") == 1);

    REQUIRE(store.load(juce::String::fromUTF8("Czysty start")));
    CHECK(presetValues(p) != wanted);

    // What a preset must not touch stays as it is.
    setParam(p, ids::quality, 2.0f);
    setParam(p, ids::delta, 1.0f);
    setParam(p, ids::bypass, 1.0f);
    setParam(p, ids::solo2, 1.0f);
    pump(50);
    const int latency = p.getLatencySamples();
    REQUIRE(store.load("moje"));
    CHECK(presetValues(p) == wanted);
    CHECK(getParam(p, ids::quality) == doctest::Approx(2.0f));
    CHECK(getParam(p, ids::delta) == doctest::Approx(1.0f));
    CHECK(getParam(p, ids::bypass) == doctest::Approx(1.0f));
    CHECK(getParam(p, ids::solo2) == doctest::Approx(1.0f));
    pump(50);
    CHECK(p.getLatencySamples() == latency);

    CHECK(store.remove("moje"));
    CHECK_FALSE(store.names().contains("moje"));
    CHECK_FALSE(store.load("moje"));
    CHECK_FALSE(store.remove(juce::String::fromUTF8("Czysty start")));
}

TEST_CASE("factory presets are valid") {
    const auto presets = factoryPresets();
    CHECK(presets.size() >= 10);
    const auto voice = wers::test::loudPhrase(48000.0, 5.0, -6.0);
    const auto levelDb = [](const juce::AudioBuffer<float>& b, int from, int to) {
        wers::KWeighting weight;
        weight.prepare(48000.0);
        double energy = 0.0;
        for (int i = 0; i < to; ++i) {
            const double v = weight.tick(static_cast<double>(b.getSample(0, i)));
            if (i >= from) energy += v * v;
        }
        return 10.0 * std::log10(energy / (to - from) + 1e-30);
    };
    juce::StringArray seen;
    for (const auto& preset : presets) {
        CAPTURE(juce::String::fromUTF8(preset.name));
        CHECK_FALSE(seen.contains(juce::String::fromUTF8(preset.name)));
        seen.add(juce::String::fromUTF8(preset.name));
        BarwaProcessor p;
        for (const auto& [id, value] : preset.values) {
            auto* parameter = p.state().getParameter(id);
            REQUIRE(parameter != nullptr);
            CHECK_FALSE(kExcluded.contains(id));
            const auto range = p.state().getParameterRange(id);
            CHECK(value >= range.start);
            CHECK(value <= range.end);
        }
        TempFolder folder;
        wers::ui::PresetStore store(p.state(), folder.path, presets, kExcluded);
        REQUIRE(store.load(juce::String::fromUTF8(preset.name)));
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> audio(2, static_cast<int>(voice.size()));
        audio.copyFrom(0, 0, voice.data(), audio.getNumSamples());
        audio.copyFrom(1, 0, voice.data(), audio.getNumSamples());
        juce::AudioBuffer<float> dry;
        dry.makeCopyOf(audio);
        runBlocks(p, audio, 256);
        for (int i = 0; i < audio.getNumSamples(); ++i) REQUIRE(std::isfinite(audio.getSample(0, i)));
        const int from = 96000, to = audio.getNumSamples();
        const double change = levelDb(audio, from, to) - levelDb(dry, from, to);
        MESSAGE(juce::String::fromUTF8(preset.name) << ": level " << change << " dB");
        CHECK(std::abs(change) <= 1.0);
    }
}

TEST_CASE("a preset missing a value uses the neutral default") {
    TempFolder folder;
    BarwaProcessor p;
    wers::ui::PresetStore store(p.state(), folder.path, factoryPresets(), kExcluded);
    setParam(p, ids::tone, 70.0f);
    setParam(p, ids::drive1, 20.0f);
    juce::String error;
    REQUIRE(store.save("stary", error));
    const auto file = folder.path.getChildFile("stary.xml");
    auto xml = juce::parseXML(file);
    REQUIRE(xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute("id") == ids::tone) { xml->removeChildElement(child, true); break; }
    REQUIRE(xml->writeTo(file));
    setParam(p, ids::tone, 40.0f);
    REQUIRE(store.load("stary"));
    CHECK(getParam(p, ids::tone) == doctest::Approx(0.0f));
    CHECK(getParam(p, ids::drive1) == doctest::Approx(20.0f));
}

TEST_CASE("A and B are kept in the project") {
    BarwaProcessor a;
    auto& store = a.presets();
    setParam(a, ids::drive1, 10.0f);
    store.storeSlot(0);
    setParam(a, ids::drive1, 20.0f);
    store.storeSlot(1);
    setParam(a, ids::quality, 2.0f);
    store.recallSlot(0);
    CHECK(getParam(a, ids::drive1) == doctest::Approx(10.0f));
    CHECK(getParam(a, ids::quality) == doctest::Approx(2.0f)); // slots leave out what presets leave out
    store.setActiveSlot(1);

    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    BarwaProcessor b;
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(b.presets().activeSlot() == 1);
    b.presets().recallSlot(1);
    CHECK(getParam(b, ids::drive1) == doctest::Approx(20.0f));
    b.presets().recallSlot(0);
    CHECK(getParam(b, ids::drive1) == doctest::Approx(10.0f));

    // The name that is shown goes with the slot.
    BarwaProcessor named;
    TempFolder namedFolder;
    wers::ui::PresetStore namedStore(named.state(), namedFolder.path, factoryPresets(), kExcluded);
    REQUIRE(namedStore.load("Rage"));
    namedStore.storeSlot(0);
    REQUIRE(namedStore.load(juce::String::fromUTF8("Dubel w tle")));
    namedStore.storeSlot(1);
    namedStore.recallSlot(0);
    CHECK(namedStore.currentName() == "Rage");
    namedStore.recallSlot(1);
    CHECK(namedStore.currentName() == juce::String::fromUTF8("Dubel w tle"));
    const auto kept = namedStore.toTree();
    namedStore.load("Rage");
    namedStore.fromTree(kept);
    namedStore.recallSlot(1);
    CHECK(namedStore.currentName() == juce::String::fromUTF8("Dubel w tle"));

    // A slot that was never stored changes nothing, and copying fills it.
    BarwaProcessor c;
    setParam(c, ids::drive1, 15.0f);
    c.presets().recallSlot(1);
    CHECK(getParam(c, ids::drive1) == doctest::Approx(15.0f));
    c.presets().storeSlot(0);
    c.presets().copySlot(0, 1);
    setParam(c, ids::drive1, 1.0f);
    c.presets().recallSlot(1);
    CHECK(getParam(c, ids::drive1) == doctest::Approx(15.0f));

    // The state of a project is still the state of the parameters only for the host's own use.
    const auto loaded = allValues(b);
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(allValues(b) == allValues(a));
    CHECK(loaded.size() == allValues(a).size());
}

TEST_CASE("awkward preset files") {
    const auto polish = juce::String::fromUTF8("Żółć ą ę");
    TempFolder folder;
    BarwaProcessor p;
    wers::ui::PresetStore store(p.state(), folder.path, factoryPresets(), kExcluded);
    juce::String error;

    SUBCASE("a Polish name") {
        setParam(p, ids::drive1, 9.0f);
        REQUIRE(store.save(polish, error));
        CHECK(store.names().contains(polish));
        setParam(p, ids::drive1, 1.0f);
        REQUIRE(store.load(polish));
        CHECK(getParam(p, ids::drive1) == doctest::Approx(9.0f));
        CHECK(store.remove(polish));
    }
    SUBCASE("a folder that does not exist yet is created") {
        wers::ui::PresetStore later(p.state(), folder.path.getChildFile("a").getChildFile("b"), factoryPresets(), kExcluded);
        CHECK(later.names().size() == static_cast<int>(factoryPresets().size()));
        CHECK(later.save("nowy", error));
        CHECK(later.names().contains("nowy"));
    }
    SUBCASE("a folder that cannot be made") {
        const auto file = folder.path.getChildFile("to jest plik");
        REQUIRE(file.create().wasOk());
        wers::ui::PresetStore blocked(p.state(), file.getChildFile("presety"), factoryPresets(), kExcluded);
        CHECK(blocked.names().size() == static_cast<int>(factoryPresets().size()));
        CHECK_FALSE(blocked.save("x", error));
        CHECK(error.isNotEmpty());
        CHECK_FALSE(blocked.load("x"));
    }
    SUBCASE("a file that cannot be written") {
        REQUIRE(folder.path.getChildFile("zablokowany.xml").createDirectory().wasOk());
        CHECK_FALSE(store.save("zablokowany", error));
        CHECK(error.isNotEmpty());
    }
    SUBCASE("damaged and foreign files are left out") {
        setParam(p, ids::drive1, 9.0f);
        REQUIRE(folder.path.getChildFile("zepsuty.xml").replaceWithText("<WERS_PRESET plugin=\"BARWA\" version=\"1\"><PARAM id="));
        REQUIRE(folder.path.getChildFile("obcy.xml").replaceWithText("<WERS_PRESET plugin=\"AKCENT\" version=\"1\"/>"));
        REQUIRE(folder.path.getChildFile("inny.xml").replaceWithText("<COS_INNEGO/>"));
        REQUIRE(folder.path.getChildFile("pusty.xml").create().wasOk());
        REQUIRE(folder.path.getChildFile("notatki.txt").replaceWithText("nie preset"));
        const auto names = store.names();
        for (const char* bad : { "zepsuty", "obcy", "inny", "pusty", "notatki" }) {
            CHECK_FALSE(names.contains(bad));
            CHECK_FALSE(store.load(bad));
        }
        CHECK(getParam(p, ids::drive1) == doctest::Approx(9.0f));
    }
    SUBCASE("values that make no sense") {
        REQUIRE(folder.path.getChildFile("dziwny.xml").replaceWithText(
            "<WERS_PRESET plugin=\"BARWA\" version=\"1\"><PARAM id=\"drive1\" value=\"9999\"/><PARAM id=\"tone\" value=\"nan\"/><PARAM id=\"nieznany\" value=\"1\"/><PARAM id=\"quality\" value=\"0\"/></WERS_PRESET>"));
        REQUIRE(store.load("dziwny"));
        CHECK(getParam(p, ids::drive1) == doctest::Approx(36.0f));
        CHECK(getParam(p, ids::tone) == doctest::Approx(0.0f));
        CHECK(getParam(p, ids::quality) == doctest::Approx(1.0f));
    }
    SUBCASE("a file copied by hand keeps working under its own name") {
        setParam(p, ids::drive1, 11.0f);
        REQUIRE(store.save("wzorzec", error));
        const juce::String copied("Klej, mocny #2 @ 50% ^^");
        const auto file = folder.path.getChildFile(copied + ".xml");
        REQUIRE(folder.path.getChildFile("wzorzec.xml").copyFileTo(file));
        CHECK(store.names().contains(copied));
        setParam(p, ids::drive1, 1.0f);
        REQUIRE(store.load(copied));
        CHECK(getParam(p, ids::drive1) == doctest::Approx(11.0f));
        CHECK(store.currentName() == copied);
        CHECK(store.remove(copied));
        CHECK_FALSE(file.exists());
    }
    SUBCASE("a name cannot leave the folder") {
        // A name can come from a project file, so it must not reach anything outside the folder.
        const auto inner = folder.path.getChildFile("presety");
        REQUIRE(inner.createDirectory().wasOk());
        const auto outside = folder.path.getChildFile("poza.xml");
        REQUIRE(outside.replaceWithText("<WERS_PRESET plugin=\"BARWA\" version=\"1\"/>"));
        wers::ui::PresetStore shut(p.state(), inner, factoryPresets(), kExcluded);
        for (const char* name : { "../poza", "..\\poza", "/poza" }) {
            CAPTURE(juce::String(name));
            CHECK_FALSE(shut.load(name));
            CHECK_FALSE(shut.remove(name));
        }
        CHECK(outside.existsAsFile());
    }
    SUBCASE("names that cannot be used") {
        CHECK_FALSE(store.save("", error));
        CHECK(error.isNotEmpty());
        error.clear();
        CHECK_FALSE(store.save("   ", error));
        CHECK(error.isNotEmpty());
        error.clear();
        // A factory preset cannot be overwritten.
        CHECK_FALSE(store.save(juce::String::fromUTF8("Czysty start"), error));
        CHECK(error.isNotEmpty());
        // A name with characters a file cannot have still gets a working preset.
        error.clear();
        CHECK(store.save("a/b:c", error));
        CHECK(store.names().size() == static_cast<int>(factoryPresets().size()) + 1);
    }
}

TEST_CASE("the preset bar walks through presets and A and B") {
    TempFolder folder;
    BarwaProcessor p;
    wers::ui::PresetStore store(p.state(), folder.path, factoryPresets(), kExcluded);
    wers::ui::PresetBar bar(store);
    bar.setBounds(0, 0, 500, 36);
    const auto names = store.names();
    bar.step(1);
    CHECK(store.currentName() == names[0]);
    bar.step(1);
    CHECK(store.currentName() == names[1]);
    bar.step(-1);
    bar.step(-1);
    CHECK(store.currentName() == names[names.size() - 1]);
    bar.step(1);
    CHECK(store.currentName() == names[0]);

    // Going to B keeps A, and going back brings it again.
    setParam(p, ids::drive1, 10.0f);
    CHECK(store.activeSlot() == 0);
    bar.pickSlot(1);
    CHECK(store.activeSlot() == 1);
    CHECK(getParam(p, ids::drive1) == doctest::Approx(10.0f));
    setParam(p, ids::drive1, 25.0f);
    bar.pickSlot(0);
    CHECK(getParam(p, ids::drive1) == doctest::Approx(10.0f));
    bar.pickSlot(1);
    CHECK(getParam(p, ids::drive1) == doctest::Approx(25.0f));
    bar.copyToOtherSlot();
    bar.pickSlot(0);
    CHECK(getParam(p, ids::drive1) == doctest::Approx(25.0f));

    bar.saveAs(juce::String::fromUTF8("Mój"));
    CHECK(store.currentName() == juce::String::fromUTF8("Mój"));
    CHECK(store.names().contains(juce::String::fromUTF8("Mój")));
    CHECK(bar.shownName() == juce::String::fromUTF8("Mój"));

    // What goes wrong is said on the bar, where the name usually is, and it fits there.
    bar.saveAs("");
    CHECK(bar.shownName() == juce::String::fromUTF8(wers::ui::text::presetNeedsName));
    CHECK(store.currentName() == juce::String::fromUTF8("Mój"));
    bar.loadNamed("nie ma takiego");
    CHECK(bar.shownName() == juce::String::fromUTF8(wers::ui::text::presetNoRead));
    for (const char* message : { wers::ui::text::presetNeedsName, wers::ui::text::presetIsFactory, wers::ui::text::presetNoFolder, wers::ui::text::presetNoWrite, wers::ui::text::presetNoRead, wers::ui::text::presetNoDelete }) {
        CAPTURE(juce::String::fromUTF8(message));
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(wers::ui::uiFont(500, 14.0f), juce::String::fromUTF8(message), 0.0f, 0.0f);
        CHECK(glyphs.getBoundingBox(0, -1, true).getWidth() < 205.0f);
    }

    // After a preset disappears, the arrows still get past its place.
    const auto gone = folder.path.getChildFile(juce::String::fromUTF8("Mój.xml"));
    REQUIRE(gone.deleteFile());
    bar.step(1);
    CHECK(store.currentName() == names[0]);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
