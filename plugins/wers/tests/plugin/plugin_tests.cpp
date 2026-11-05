// Tests of the whole plugin through the same entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ReaperHostInterface.h"
#include "ui/PluginEditor.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace wers;

// Allocation counter for the audio thread test.
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
class FakeHead final : public juce::AudioPlayHead {
public:
    juce::int64 samples = 0; bool playing = true; bool known = true; double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override {
        if (!known) return {};
        PositionInfo i;
        i.setTimeInSamples(samples); i.setIsPlaying(playing); i.setBpm(bpm);
        return i;
    }
};

void setParam(WersProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

juce::AudioBuffer<float> noiseBuffer(int ch, int n, float gain = 0.1f, int seed = 1) {
    juce::AudioBuffer<float> b(ch, n);
    juce::Random r(seed);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i) b.setSample(c, i, (r.nextFloat() * 2.0f - 1.0f) * gain);
    return b;
}

juce::AudioBuffer<float> voiceLike(int n, double fs) {
    juce::AudioBuffer<float> b(2, n);
    juce::Random r(3);
    for (int i = 0; i < n; ++i) {
        const double t = i / fs;
        const double cycle = std::fmod(t, 0.42);
        float v = 0.0f;
        if (cycle < 0.2) { for (int h = 1; h < 12; ++h) v += std::sin(2.0f * 3.14159f * 140.0f * h * static_cast<float>(t)) / static_cast<float>(h); v *= 0.12f; }
        else if (cycle < 0.28) { v = (r.nextFloat() * 2.0f - 1.0f) * 0.05f; }
        else if (cycle < 0.38) { for (int h = 1; h < 12; ++h) v += std::sin(2.0f * 3.14159f * 180.0f * h * static_cast<float>(t)) / static_cast<float>(h); v *= 0.12f; }
        b.setSample(0, i, v); b.setSample(1, i, v);
    }
    return b;
}

void runBlocks(WersProcessor& p, FakeHead& head, juce::AudioBuffer<float>& audio, int block) {
    juce::MidiBuffer midi;
    for (int pos = 0; pos < audio.getNumSamples(); pos += block) {
        const int n = juce::jmin(block, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), pos, n);
        p.processBlock(view, midi);
        head.samples += n;
    }
}
} // namespace

TEST_CASE("every role processes without allocating on the audio thread") {
    for (int role : { 0, 1, 2 }) {
        WersProcessor p;
        FakeHead head;
        p.setPlayHead(&head);
        setParam(p, ids::role, static_cast<float>(role));
        pump(150);
        CHECK(static_cast<int>(p.role()) == role);
        setParam(p, ids::density, 70); setParam(p, ids::clarity, 60); setParam(p, ids::space, 40); setParam(p, ids::monDevice, 0); setParam(p, ids::monNorm, 1);
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        auto audio = noiseBuffer(2, 48000);
        gAllocations = 0;
        gCount = true;
        runBlocks(p, head, audio, 512);
        gCount = false;
        CHECK(gAllocations.load() == 0);
        p.setPlayHead(nullptr);
    }
}

TEST_CASE("listening in the vocal role does not allocate either") {
    WersProcessor p;
    FakeHead head;
    p.setPlayHead(&head);
    p.prepareToPlay(44100.0, 256);
    p.startAnalysis();
    auto audio = voiceLike(44100, 44100.0);
    gAllocations = 0;
    gCount = true;
    runBlocks(p, head, audio, 256);
    gCount = false;
    CHECK(gAllocations.load() == 0);
    p.session().cancelListening();
    p.setPlayHead(nullptr);
}

TEST_CASE("neutral settings pass audio with exactly the reported latency in every role") {
    for (int role : { 0, 1, 2 }) {
        WersProcessor p;
        setParam(p, ids::role, static_cast<float>(role));
        setParam(p, ids::place, 0);
        pump(150);
        p.prepareToPlay(48000.0, 480);
        const int lat = p.getLatencySamples();
        if (role == 0) CHECK(lat > 0); else CHECK(lat == 0);
        auto in = noiseBuffer(2, 24000);
        auto out = in;
        FakeHead head;
        p.setPlayHead(&head);
        runBlocks(p, head, out, 480);
        p.setPlayHead(nullptr);
        bool same = true;
        for (int c = 0; c < 2; ++c) for (int i = lat; i < 24000; ++i) same = same && out.getSample(c, i) == in.getSample(c, i - lat);
        CHECK(same);
    }
}

TEST_CASE("bypass keeps the latency and takes effect without a click") {
    WersProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, ids::density, 80);
    const int lat = p.getLatencySamples();
    auto in = noiseBuffer(2, 48000);
    auto out = in;
    FakeHead head;
    p.setPlayHead(&head);
    runBlocks(p, head, out, 256);
    setParam(p, ids::bypass, 1.0f);
    auto out2 = in;
    runBlocks(p, head, out2, 256);
    bool same = true;
    for (int i = 4800 + lat; i < 48000; ++i) same = same && out2.getSample(0, i) == in.getSample(0, i - lat);
    CHECK(same);
    float maxJump = 0.0f;
    for (int i = 1; i < 4800; ++i) maxJump = juce::jmax(maxJump, std::abs(out2.getSample(0, i) - out2.getSample(0, i - 1)));
    CHECK(maxJump < 0.45f);
    p.setPlayHead(nullptr);
}

TEST_CASE("blocks larger than prepared, single samples and silence all work") {
    for (int role : { 0, 1, 2 }) {
        WersProcessor p;
        setParam(p, ids::role, static_cast<float>(role));
        pump(150);
        p.prepareToPlay(96000.0, 64);
        juce::MidiBuffer midi;
        FakeHead head;
        p.setPlayHead(&head);
        auto big = noiseBuffer(2, 20000);
        p.processBlock(big, midi);
        for (int i = 0; i < 100; ++i) { juce::AudioBuffer<float> one(2, 1); one.clear(); p.processBlock(one, midi); }
        juce::AudioBuffer<float> empty(2, 0);
        p.processBlock(empty, midi);
        head.known = false;
        auto again = noiseBuffer(2, 4096);
        p.processBlock(again, midi);
        bool ok = true;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4096; ++i) ok = ok && std::isfinite(again.getSample(c, i));
        CHECK(ok);
        p.setPlayHead(nullptr);
    }
}

TEST_CASE("mono layout and sample rate changes") {
    WersProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add(juce::AudioChannelSet::mono());
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    mono.inputBuses.add(juce::AudioChannelSet::disabled());
    CHECK(p.setBusesLayout(mono));
    for (double fs : { 44100.0, 192000.0, 22050.0, 48000.0 }) {
        p.prepareToPlay(fs, 512);
        auto b = noiseBuffer(1, 2048);
        juce::MidiBuffer m;
        p.processBlock(b, m);
    }
    juce::AudioProcessor::BusesLayout weird;
    weird.inputBuses.add(juce::AudioChannelSet::stereo());
    weird.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(!p.isBusesLayoutSupported(weird));
}

TEST_CASE("full round: two linked instances, analysis, fix, render, save and reload") {
    const double fs = 48000.0;
    auto vocalProc = std::make_unique<WersProcessor>();
    auto beatProc = std::make_unique<WersProcessor>();
    setParam(*vocalProc, ids::group, 5.0f);
    setParam(*beatProc, ids::group, 5.0f);
    setParam(*beatProc, ids::role, 1.0f);
    pump(300);
    REQUIRE(beatProc->role() == Role::Beat);
    REQUIRE(vocalProc->group() == beatProc->group());
    CHECK(vocalProc->group()->beatCount() == 1);
    vocalProc->prepareToPlay(fs, 512);
    beatProc->prepareToPlay(fs, 512);

    const int n = static_cast<int>(fs * 8);
    auto vocal = voiceLike(n, fs);
    auto beat = noiseBuffer(2, n, 0.6f, 9);
    FakeHead vh, bh;
    vocalProc->setPlayHead(&vh);
    beatProc->setPlayHead(&bh);
    vocalProc->startAnalysis();
    auto v1 = vocal, b1 = beat;
    runBlocks(*vocalProc, vh, v1, 512);
    runBlocks(*beatProc, bh, b1, 512);
    vh.playing = false;
    // The session starts on its own when the transport stops.
    for (int i = 0; i < 400 && vocalProc->session().phase() != SessionPhase::Ready; ++i) {
        juce::AudioBuffer<float> quiet(2, 512); quiet.clear();
        juce::MidiBuffer m;
        vocalProc->processBlock(quiet, m);
        pump(25);
    }
    REQUIRE(vocalProc->session().phase() == SessionPhase::Ready);
    auto res = vocalProc->session().result();
    REQUIRE(res);
    CHECK(res->hasBeat);
    REQUIRE(res->words.size() > 5);

    // Some fixes are only a vocal boost. Fix words until one of them also cuts the beat.
    int bad = -1, boosted = -1;
    for (size_t i = 0; i < res->words.size() && bad < 0; ++i) {
        if (res->words[i].verdict[0].score >= kScoreTarget) continue;
        vocalProc->session().fixWord(static_cast<int>(i), Device::Phone);
        vocalProc->session().waitIdle();
        res = vocalProc->session().result();
        const auto& fx = res->words[i].fix;
        REQUIRE(fx.active);
        const auto positive = [](float x) { return x > 0.0f; };
        if (boosted < 0 && std::any_of(fx.vocalBoostDb.begin(), fx.vocalBoostDb.end(), positive)) boosted = static_cast<int>(i);
        if (std::any_of(fx.beatCutDb.begin(), fx.beatCutDb.end(), positive)) bad = static_cast<int>(i);
    }
    REQUIRE(bad >= 0);
    const auto word = res->words[static_cast<size_t>(bad)];

    // The vocal instance applies the boost inside the word and nowhere near a word without a fix.
    if (boosted >= 0) {
        const auto boostedWord = res->words[static_cast<size_t>(boosted)];
        setParam(*vocalProc, ids::fixes, 0.0f);
        vh.samples = 0; vh.playing = true;
        vocalProc->prepareToPlay(fs, 512);
        auto dryRun = vocal;
        runBlocks(*vocalProc, vh, dryRun, 512);
        setParam(*vocalProc, ids::fixes, 1.0f);
        vh.samples = 0;
        vocalProc->prepareToPlay(fs, 512);
        auto fixedRun = vocal;
        runBlocks(*vocalProc, vh, fixedRun, 512);
        const int lat = vocalProc->getLatencySamples();
        double inside = 0;
        for (int i = static_cast<int>(boostedWord.t0 * fs) + lat; i < static_cast<int>(boostedWord.t1 * fs) + lat; ++i) {
            const double d = fixedRun.getSample(0, i) - dryRun.getSample(0, i);
            inside += d * d;
        }
        CHECK(inside > 0.0);
        vh.playing = false;
    }

    // The beat instance now applies the cut at the word, and leaves other places alone.
    setParam(*beatProc, ids::place, 0);
    vh.samples = bh.samples = 0;
    vh.playing = bh.playing = true;
    auto b2 = beat;
    beatProc->prepareToPlay(fs, 512);
    runBlocks(*beatProc, bh, b2, 512);
    double diffIn = 0, diffOut = 0;
    const int from = static_cast<int>(word.t0 * fs), to = static_cast<int>(word.t1 * fs);
    for (int i = from; i < to; ++i) { const double d = b2.getSample(0, i) - beat.getSample(0, i); diffIn += d * d; }
    const int quietFrom = to + static_cast<int>(fs * 0.5);
    bool haveQuiet = false;
    if (quietFrom + (to - from) < n) {
        bool clear = true;
        for (const auto& w : res->words) if (w.fix.active && w.t0 * fs < quietFrom + (to - from) && w.t1 * fs > quietFrom) clear = false;
        if (clear) { haveQuiet = true; for (int i = quietFrom; i < quietFrom + (to - from); ++i) { const double d = b2.getSample(0, i) - beat.getSample(0, i); diffOut += d * d; } }
    }
    CHECK(diffIn > 0.0);
    MESSAGE("energy of the change inside the word ", diffIn, ", half a second later ", diffOut);
    if (haveQuiet) CHECK(diffOut < diffIn * 0.02);

    // Fixes off: the beat passes untouched.
    setParam(*vocalProc, ids::fixes, 0.0f);
    pump(250);
    bh.samples = 0;
    auto b3 = beat;
    beatProc->prepareToPlay(fs, 512);
    runBlocks(*beatProc, bh, b3, 512);
    double diffOff = 0;
    for (int i = 0; i < n; ++i) { const double d = b3.getSample(0, i) - beat.getSample(0, i); diffOff += d * d; }
    CHECK(diffOff == 0.0);
    setParam(*vocalProc, ids::fixes, 1.0f);

    // Save the vocal instance, destroy it and bring it back.
    juce::MemoryBlock saved;
    vocalProc->getStateInformation(saved);
    CHECK(saved.getSize() > 1000);
    vocalProc->setPlayHead(nullptr);
    vocalProc.reset();
    pump(50);
    CHECK(beatProc->group()->vocalCount() == 0);

    auto restored = std::make_unique<WersProcessor>();
    restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    pump(300);
    CHECK(restored->role() == Role::Vocal);
    CHECK(restored->groupNumber() == 6);
    auto res2 = restored->session().result();
    REQUIRE(res2);
    CHECK(res2->words.size() == res->words.size());
    CHECK(res2->words[static_cast<size_t>(bad)].fix.active);
    CHECK(restored->group()->vocalCount() == 1);
    bool planThere = false;
    for (int s = 0; s < kLinkSlots; ++s) if (const Plan* pl = restored->group()->plan(s)) planThere = planThere || !pl->events.empty();
    CHECK(planThere);

    beatProc->setPlayHead(nullptr);
}

namespace {
// Writes the fixture below from synthetic sound. Run once per version whose projects must keep opening.
void writeStateFixture(const juce::File& file) {
    const double fs = 48000.0;
    auto vocalProc = std::make_unique<WersProcessor>();
    auto beatProc = std::make_unique<WersProcessor>();
    setParam(*vocalProc, ids::group, 5.0f);
    setParam(*beatProc, ids::group, 5.0f);
    setParam(*beatProc, ids::role, 1.0f);
    pump(300);
    vocalProc->prepareToPlay(fs, 512);
    beatProc->prepareToPlay(fs, 512);
    const int n = static_cast<int>(fs * 8);
    auto vocal = voiceLike(n, fs);
    auto beat = noiseBuffer(2, n, 0.6f, 9);
    FakeHead vh, bh;
    vocalProc->setPlayHead(&vh);
    beatProc->setPlayHead(&bh);
    vocalProc->startAnalysis();
    runBlocks(*vocalProc, vh, vocal, 512);
    runBlocks(*beatProc, bh, beat, 512);
    vh.playing = false;
    for (int i = 0; i < 400 && vocalProc->session().phase() != SessionPhase::Ready; ++i) {
        juce::AudioBuffer<float> quiet(2, 512); quiet.clear();
        juce::MidiBuffer m;
        vocalProc->processBlock(quiet, m);
        pump(25);
    }
    REQUIRE(vocalProc->session().phase() == SessionPhase::Ready);
    vocalProc->session().fixAllBelow(kScoreTarget, Device::Phone);
    vocalProc->session().waitIdle();
    juce::MemoryBlock saved;
    vocalProc->getStateInformation(saved);
    REQUIRE(file.getParentDirectory().createDirectory().wasOk());
    REQUIRE(file.replaceWithData(saved.getData(), saved.getSize()));
    vocalProc->setPlayHead(nullptr);
    beatProc->setPlayHead(nullptr);
}
} // namespace

TEST_CASE("a project saved by version 0.1.0 still opens") {
    if (const char* out = std::getenv("WERS_WRITE_STATE_FIXTURE")) {
        writeStateFixture(juce::File(juce::String(out)));
        return;
    }
    juce::MemoryBlock data;
    REQUIRE(juce::File(WERS_TEST_DATA_DIR).getChildFile("state_0_1_0.bin").loadFileAsData(data));
    WersProcessor p;
    p.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    pump(300);
    CHECK(p.role() == Role::Vocal);
    CHECK(p.groupNumber() == 6);
    CHECK(p.session().phase() == SessionPhase::Ready);
    const auto res = p.session().result();
    REQUIRE(res);
    CHECK(res->words.size() >= 5);
    CHECK(std::any_of(res->words.begin(), res->words.end(), [](const Word& w) { return w.fix.active; }));
    REQUIRE(p.group() != nullptr);
    bool planThere = false;
    for (int s = 0; s < kLinkSlots; ++s) if (const Plan* plan = p.group()->plan(s)) planThere = planThere || !plan->events.empty();
    CHECK(planThere);
}

TEST_CASE("audio keeps running while the message thread changes everything") {
    const double fs = 48000.0;
    WersProcessor vocal, beat;
    setParam(vocal, ids::group, 3.0f);
    setParam(beat, ids::group, 3.0f);
    setParam(beat, ids::role, 1.0f);
    pump(200);
    vocal.prepareToPlay(fs, 512);
    beat.prepareToPlay(fs, 512);
    FakeHead vh, bh;
    vocal.setPlayHead(&vh);
    beat.setPlayHead(&bh);
    auto vocalAudio = voiceLike(static_cast<int>(fs * 20), fs);
    auto beatAudio = noiseBuffer(2, static_cast<int>(fs * 20), 0.3f, 4);

    std::atomic<bool> run{ true };
    std::atomic<long> blocks{ 0 };
    std::atomic<bool> finite{ true };
    std::thread audio([&] {
        juce::AudioBuffer<float> v(2, 512), b(2, 512);
        juce::MidiBuffer midi;
        int pos = 0;
        while (run.load()) {
            if (pos + 512 > vocalAudio.getNumSamples()) { pos = 0; }
            vh.samples = bh.samples = pos;
            for (int c = 0; c < 2; ++c) {
                v.copyFrom(c, 0, vocalAudio, c, pos, 512);
                b.copyFrom(c, 0, beatAudio, c, pos, 512);
            }
            vocal.processBlock(v, midi);
            beat.processBlock(b, midi);
            for (int i = 0; i < 512; i += 37) if (!std::isfinite(v.getSample(0, i)) || !std::isfinite(b.getSample(0, i))) finite.store(false);
            pos += 512;
            blocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
        }
    });

    for (int round = 0; round < 6; ++round) {
        vocal.startAnalysis();
        pump(250);
        vocal.session().stopListeningAndAnalyse();
        vocal.session().waitIdle();
        vocal.session().fixAllBelow(kScoreFades, Device::Phone);
        pump(30);
        juce::MemoryBlock saved;
        vocal.getStateInformation(saved);
        vocal.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        pump(30);
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(vocal.createEditor());
            dynamic_cast<WersEditor*>(ed.get())->updateNow();
            pump(50);
        }
        vocal.session().clearAllFixes();
        setParam(beat, ids::place, static_cast<float>(round * 15));
        setParam(vocal, ids::density, static_cast<float>(round * 15));
        setParam(vocal, ids::space, static_cast<float>(round * 15));
        setParam(vocal, ids::bypass, round % 2 == 0 ? 1.0f : 0.0f);
        pump(30);
        vocal.chooseRole(Role::Master);
        pump(30);
        vocal.chooseRole(Role::Vocal);
        vocal.cycleGroup();
        beat.cycleGroup();
        pump(30);
        beat.chooseRole(Role::Vocal);
        pump(20);
        beat.chooseRole(Role::Beat);
        pump(20);
    }
    run.store(false);
    audio.join();
    vocal.setPlayHead(nullptr);
    beat.setPlayHead(nullptr);
    CHECK(blocks.load() > 100);
    CHECK(finite.load());
}

TEST_CASE("damaged saved state never harms the plugin") {
    WersProcessor good;
    juce::MemoryBlock saved;
    good.getStateInformation(saved);
    WersProcessor p;
    p.setStateInformation(nullptr, 0);
    p.setStateInformation("abc", 3);
    juce::Random r(11);
    for (int attempt = 0; attempt < 40; ++attempt) {
        juce::MemoryBlock m(saved);
        if (attempt % 2 == 0) m.setSize(static_cast<size_t>(r.nextInt(static_cast<int>(saved.getSize()))) + 1, false);
        for (int k = 0; k < 8 && m.getSize() > 0; ++k) static_cast<uint8_t*>(m.getData())[static_cast<size_t>(r.nextInt(static_cast<int>(m.getSize())))] = static_cast<uint8_t>(r.nextInt(256));
        p.setStateInformation(m.getData(), static_cast<int>(m.getSize()));
    }
    std::vector<uint8_t> junk(200000);
    for (auto& b : junk) b = static_cast<uint8_t>(r.nextInt(256));
    p.setStateInformation(junk.data(), static_cast<int>(junk.size()));
    p.prepareToPlay(48000.0, 512);
    auto b = noiseBuffer(2, 2048);
    juce::MidiBuffer midi;
    p.processBlock(b, midi);
    bool ok = true;
    for (int i = 0; i < 2048; ++i) ok = ok && std::isfinite(b.getSample(0, i));
    CHECK(ok);
}

TEST_CASE("track name suggests a role once and only once") {
    const auto named = [](const char* n) { juce::AudioProcessor::TrackProperties t; t.name = juce::String(n); return t; };
    {
        WersProcessor p;
        p.updateTrackProperties(named("beat"));
        pump(200);
        CHECK(p.role() == Role::Beat);
        p.updateTrackProperties(named("main vocals"));
        pump(200);
        CHECK(p.role() == Role::Beat);
    }
    {
        WersProcessor p;
        p.updateTrackProperties(named("orbit"));
        pump(200);
        CHECK(p.role() == Role::Vocal);
    }
}

TEST_CASE("interface opens and closes in every role and state without trouble") {
    WersProcessor p;
    for (int round = 0; round < 3; ++round) {
        for (int role : { 0, 1, 2 }) {
            p.chooseRole(static_cast<Role>(role));
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            REQUIRE(ed != nullptr);
            auto* e = dynamic_cast<WersEditor*>(ed.get());
            REQUIRE(e != nullptr);
            e->updateNow();
            const auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            CHECK(img.getWidth() == ed->getWidth());
            ed->setSize(800, 503);
            pump(60);
            ed.reset();
        }
    }
}

TEST_CASE("the model downloads through the plugin's own network code" * doctest::skip()) {
    ModelManager m;
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wers_model_test.bin");
    file.deleteFile();
    m.setTargetForTests(file);
    m.startDownload();
    for (int i = 0; i < 6000 && m.state() == ModelManager::State::Downloading; ++i) pump(100);
    CHECK(m.state() == ModelManager::State::Ready);
    CHECK(file.getSize() == ModelManager::expectedBytes());
    file.deleteFile();
}

#ifdef WERS_TEST_HELPER
TEST_CASE("recognition process: words arrive with Polish letters and progress") {
    ProcessRecognizer r(juce::File(WERS_TEST_HELPER), ":fake:");
    const std::vector<float> pcm(32000, 0.0f);
    std::atomic<bool> cancel{ false };
    float last = -1.0f;
    const auto words = r.transcribe(pcm, { { 0, pcm.size() } }, {}, cancel, [&](float p) { last = p; });
    REQUIRE(words.size() == 4);
    CHECK(words[0].text == juce::String::fromUTF8("księżyc").toStdString());
    CHECK(words[3].text == juce::String::fromUTF8("żyję").toStdString());
    CHECK(words[1].t0 == doctest::Approx(0.6));
    CHECK(words[1].confidence == doctest::Approx(0.75f));
    CHECK(last == doctest::Approx(1.0f));
    CHECK(!r.failed());
}

TEST_CASE("recognition process: a crash only means no text") {
    ProcessRecognizer r(juce::File(WERS_TEST_HELPER), ":crash:");
    const std::vector<float> pcm(16000, 0.0f);
    std::atomic<bool> cancel{ false };
    CHECK(r.transcribe(pcm, { { 0, pcm.size() } }, {}, cancel, {}).empty());
    CHECK(r.failed());
}

TEST_CASE("recognition process: a model that cannot load is reported as a failure") {
    ProcessRecognizer r(juce::File(WERS_TEST_HELPER), "this file does not exist.bin");
    const std::vector<float> pcm(16000, 0.0f);
    std::atomic<bool> cancel{ false };
    CHECK(r.transcribe(pcm, { { 0, pcm.size() } }, {}, cancel, {}).empty());
    CHECK(r.failed());
}

TEST_CASE("recognition process: cancelling stops it quickly and is not a failure") {
    ProcessRecognizer r(juce::File(WERS_TEST_HELPER), ":slow:");
    const std::vector<float> pcm(16000, 0.0f);
    std::atomic<bool> cancel{ false };
    std::thread later([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); cancel.store(true); });
    const auto start = std::chrono::steady_clock::now();
    CHECK(r.transcribe(pcm, { { 0, pcm.size() } }, {}, cancel, {}).empty());
    later.join();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
    CHECK(!r.failed());
}
#endif

TEST_CASE("recognition process: a missing program is a failure and nothing more") {
    ProcessRecognizer r(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wers_no_such_program"), "model.bin");
    const std::vector<float> pcm(16000, 0.0f);
    std::atomic<bool> cancel{ false };
    CHECK(r.transcribe(pcm, { { 0, pcm.size() } }, {}, cancel, {}).empty());
    CHECK(r.failed());
}

namespace {
// Plays REAPER: a vocal track at -6 dB inside a folder at +2 dB, as the real host reports them.
double fakeValue(void* track, const char* name) {
    const auto id = reinterpret_cast<std::intptr_t>(track);
    if (std::strcmp(name, "D_VOL") == 0) return id == 1 ? 0.5 : 1.2589254117941673;
    if (std::strcmp(name, "B_MAINSEND") == 0) return 1.0;
    return 0.0;
}
void* fakeParent(void* track) { return reinterpret_cast<std::intptr_t>(track) == 1 ? reinterpret_cast<void*>(std::intptr_t{ 2 }) : nullptr; }
bool fakeValidate(void*, void*, const char*) { return true; }

void* seekProject = nullptr;
double seekTime = -1.0;
void fakeSeek(void* project, double time, bool, bool) { seekProject = project; seekTime = time; }

class FakeReaper final : public IReaperHostApplication {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID id, void** out) override {
        if (std::memcmp(id, reaperHostIid(), sizeof(Steinberg::TUID)) == 0) { *out = this; addRef(); return Steinberg::kResultOk; }
        *out = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return static_cast<Steinberg::uint32>(++refs); }
    Steinberg::uint32 PLUGIN_API release() override { return static_cast<Steinberg::uint32>(--refs); }
    void* PLUGIN_API getReaperApi(Steinberg::CStringA name) override {
        if (std::strcmp(name, "GetMediaTrackInfo_Value") == 0) return reinterpret_cast<void*>(&fakeValue);
        if (std::strcmp(name, "GetParentTrack") == 0) return reinterpret_cast<void*>(&fakeParent);
        if (std::strcmp(name, "ValidatePtr2") == 0) return reinterpret_cast<void*>(&fakeValidate);
        if (std::strcmp(name, "SetEditCurPos2") == 0) return reinterpret_cast<void*>(&fakeSeek);
        return nullptr;
    }
    void* PLUGIN_API getReaperParent(Steinberg::uint32 what) override {
        if (what == 1) return reinterpret_cast<void*>(std::intptr_t{ 1 });
        return what == 3 ? project : nullptr;
    }
    void* PLUGIN_API reaperExtended(Steinberg::uint32, void*, void*, void*) override { return nullptr; }
    int refs = 1;
    void* project = nullptr;
};

class NotReaper final : public Steinberg::FUnknown {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** out) override { *out = nullptr; return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};
}

TEST_CASE("REAPER: the fader of the track and of its folder are read through the host interface") {
    FakeReaper host;
    {
        ReaperBridge bridge;
        CHECK(!bridge.connected());
        CHECK(bridge.trackGainDb() == 0.0f);
        bridge.setIHostApplication(&host);
        CHECK(bridge.connected());
        CHECK(bridge.trackGainDb() == doctest::Approx(-4.02f).epsilon(0.01));
        CHECK(host.refs == 2);
    }
    CHECK(host.refs == 1);
    ReaperBridge other;
    NotReaper notReaper;
    other.setIHostApplication(&notReaper);
    CHECK(!other.connected());
    CHECK(other.trackGainDb() == 0.0f);
    other.setIHostApplication(nullptr);
    CHECK(!other.connected());
}

TEST_CASE("REAPER: a word can move the edit cursor in its own project, a little before the word") {
    int project = 0;
    FakeReaper host;
    host.project = &project;
    ReaperBridge bridge;
    CHECK(!bridge.seek(10.0)); // not in REAPER: nothing happens
    bridge.setIHostApplication(&host);
    REQUIRE(bridge.seek(10.0));
    CHECK(seekProject == &project);
    CHECK(seekTime == doctest::Approx(9.75));
    REQUIRE(bridge.seek(0.1));
    CHECK(seekTime == 0.0);
}

TEST_CASE("REAPER: a vocal instance passes the faders on to the scores and to the beat") {
    FakeReaper host;
    WersProcessor vocal;
    vocal.getVST3ClientExtensions()->setIHostApplication(&host);
    setParam(vocal, ids::group, 7.0f);
    pump(300);
    REQUIRE(vocal.group() != nullptr);
    REQUIRE(vocal.linkSlot() >= 0);
    CHECK(vocal.group()->vocalFaderDb(vocal.linkSlot()) == doctest::Approx(-4.02f).epsilon(0.01));
}

TEST_CASE("REAPER: instances in different project tabs never link, in the same project they do") {
    int projectA = 0, projectB = 0;
    FakeReaper hostA, hostA2, hostB;
    hostA.project = &projectA; hostA2.project = &projectA; hostB.project = &projectB;
    WersProcessor vocalA, beatA, vocalB;
    vocalA.getVST3ClientExtensions()->setIHostApplication(&hostA);
    beatA.getVST3ClientExtensions()->setIHostApplication(&hostA2);
    vocalB.getVST3ClientExtensions()->setIHostApplication(&hostB);
    beatA.chooseRole(Role::Beat);
    pump(300);
    CHECK(vocalA.group() == beatA.group());
    CHECK(vocalA.group() != vocalB.group());
    CHECK(vocalA.group()->beatCount() == 1);
    CHECK(vocalB.group()->beatCount() == 0);
}

TEST_CASE("every instance sees one and the same model manager") {
    WersProcessor a, b;
    CHECK(&a.model() == &b.model());
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    const int r = ctx.run();
    return r;
}
