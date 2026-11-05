// Tests of the whole plugin through the same entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "plugin/BarwaProcessor.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <thread>

using namespace barwa;

static std::atomic<long> gAllocations{ 0 };
static std::atomic<bool> gCount{ false };
void* operator new(std::size_t n) {
    if (gCount.load(std::memory_order_relaxed)) gAllocations.fetch_add(1);
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

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
