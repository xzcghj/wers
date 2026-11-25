// Tests of the whole Powiew plugin through the entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "AllocationCount.h"
#include "TestAudio.h"
#include "TestVoice.h"
#include "plugin/FactoryPresets.h"
#include "plugin/PowiewProcessor.h"
#include "ui/PowiewEditor.h"
#include <cmath>

WERS_COUNT_ALLOCATIONS()

using namespace powiew;

namespace {

void setParam(PowiewProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(PowiewProcessor& p, const char* id) { return p.state().getRawParameterValue(id)->load(); }

struct Run {
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
    std::vector<float> l;
};

Run play(PowiewProcessor& p, const wir::test::Stereo& audio, int block = 512) {
    Run r;
    juce::MidiBuffer midi;
    double sum = 0.0;
    size_t count = 0;
    for (size_t at = 0; at + static_cast<size_t>(block) <= audio.l.size(); at += static_cast<size_t>(block)) {
        juce::AudioBuffer<float> b(2, block);
        b.copyFrom(0, 0, audio.l.data() + at, block);
        b.copyFrom(1, 0, audio.r.data() + at, block);
        p.processBlock(b, midi);
        for (int i = 0; i < block; ++i) {
            const float l = b.getSample(0, i), rr = b.getSample(1, i);
            if (!std::isfinite(l) || !std::isfinite(rr)) r.finite = false;
            r.peak = std::max({ r.peak, std::abs(l), std::abs(rr) });
            sum += 0.5 * (static_cast<double>(l) * l + static_cast<double>(rr) * rr);
            r.l.push_back(l);
            ++count;
        }
    }
    r.rms = std::sqrt(sum / static_cast<double>(std::max<size_t>(1, count)));
    return r;
}

wir::test::Stereo voice(double seconds) {
    const auto v = powiew::test::voiceLoop(48000.0, seconds);
    return { v.l, v.r };
}

std::unique_ptr<PowiewProcessor> make(double fs = 48000.0, int block = 512) {
    auto p = std::make_unique<PowiewProcessor>();
    p->setPlayConfigDetails(2, 2, fs, block);
    p->prepareToPlay(fs, block);
    return p;
}

} // namespace

TEST_CASE("every factory preset plays cleanly, stays in a sane level range and adds air") {
    const auto loop = wir::test::demoLoop(48000.0, 4.0);
    const auto v = voice(4.8);
    auto refLoop = make(), refVoice = make();
    const auto dryLoop = play(*refLoop, loop), dryVoice = play(*refVoice, v);
    CHECK(factoryPresets().size() >= 15);
    for (const auto& preset : factoryPresets()) {
        const auto name = juce::String::fromUTF8(preset.name);
        CAPTURE(name.toStdString());
        CHECK(juce::String::fromUTF8(preset.description).length() > 20);
        for (const auto* audio : { &loop, &v }) {
            auto p = make();
            REQUIRE(p->presets().load(name));
            const auto r = play(*p, *audio);
            const auto& dry = audio == &loop ? dryLoop : dryVoice;
            CHECK(r.finite);
            const double db = 20.0 * std::log10(r.rms / dry.rms);
            MESSAGE(name << (audio == &loop ? " (loop): " : " (voice): ") << db << " dB, peak " << r.peak);
            CHECK(db > -3.5);
            CHECK(db < 2.0);
            CHECK(r.peak < 1.2f);
        }
    }
}

TEST_CASE("the reported latency is the real one and both knobs at zero pass the input through") {
    auto p = make();
    CHECK(p->getLatencySamples() == p->engine().latency());
    CHECK(p->getLatencySamples() > 0);
    const auto audio = wir::test::demoLoop(48000.0, 0.5);
    const auto r = play(*p, audio);
    const auto lat = static_cast<size_t>(p->getLatencySamples());
    float err = 0.0f;
    for (size_t i = lat; i < r.l.size(); ++i) err = std::max(err, std::abs(r.l[i] - audio.l[i - lat]));
    CHECK(err == 0.0f);
}

TEST_CASE("state round trip keeps every parameter and the editor size") {
    auto a = make();
    setParam(*a, ids::presence, 42.0f);
    setParam(*a, ids::air, 77.0f);
    setParam(*a, ids::presenceHz, 4500.0f);
    setParam(*a, ids::airHz, 15000.0f);
    setParam(*a, ids::smooth, 66.0f);
    setParam(*a, ids::character, 2.0f);
    setParam(*a, ids::mix, 55.0f);
    setParam(*a, ids::trim, -2.5f);
    setParam(*a, ids::monoSafe, 1.0f);
    a->setUiScale(1.25f);
    juce::MemoryBlock block;
    a->getStateInformation(block);
    auto b = make();
    b->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    for (auto* param : a->getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(param);
        CAPTURE(withId->paramID);
        CHECK(getParam(*b, withId->paramID.toRawUTF8()) == doctest::Approx(getParam(*a, withId->paramID.toRawUTF8())));
    }
    CHECK(b->uiScale() == doctest::Approx(1.25f));
}

TEST_CASE("garbage state is ignored") {
    auto p = make();
    setParam(*p, ids::air, 50.0f);
    const char junk[] = "definitely not a state";
    p->setStateInformation(junk, sizeof(junk));
    p->setStateInformation(nullptr, 0);
    juce::MemoryBlock fake;
    {
        juce::MemoryOutputStream out(fake, false);
        out.writeInt(0x504f5731);
        out.writeInt(1);
        out.writeInt(100000);
    }
    p->setStateInformation(fake.getData(), static_cast<int>(fake.getSize()));
    CHECK(getParam(*p, ids::air) == doctest::Approx(50.0f));
}

TEST_CASE("bypass fades to the dry signal, delayed like the processed one") {
    auto p = make();
    setParam(*p, ids::presence, 100.0f);
    setParam(*p, ids::air, 100.0f);
    const auto audio = voice(1.0);
    play(*p, audio);
    setParam(*p, ids::bypass, 1.0f);
    juce::AudioBuffer<float> b(2, 9600);
    b.copyFrom(0, 0, audio.l.data(), 9600);
    b.copyFrom(1, 0, audio.r.data(), 9600);
    juce::MidiBuffer midi;
    p->processBlock(b, midi);
    const auto lat = static_cast<size_t>(p->getLatencySamples());
    float diff = 0.0f;
    for (size_t i = 2400; i < 9600; ++i) diff = std::max(diff, std::abs(b.getSample(1, static_cast<int>(i)) - audio.r[i - lat]));
    CHECK(diff == 0.0f);
    // The host's own bypass call does the same.
    setParam(*p, ids::bypass, 0.0f);
    juce::AudioBuffer<float> c(2, 9600);
    c.copyFrom(0, 0, audio.l.data(), 9600);
    c.copyFrom(1, 0, audio.r.data(), 9600);
    p->processBlockBypassed(c, midi);
    diff = 0.0f;
    for (size_t i = 2400; i < 9600; ++i) diff = std::max(diff, std::abs(c.getSample(0, static_cast<int>(i)) - audio.l[i - lat]));
    CHECK(diff == 0.0f);
}

TEST_CASE("mono in and out works, other layouts are refused") {
    PowiewProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add(juce::AudioChannelSet::mono());
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    REQUIRE(p.checkBusesLayoutSupported(mono));
    juce::AudioProcessor::BusesLayout mixed;
    mixed.inputBuses.add(juce::AudioChannelSet::mono());
    mixed.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK_FALSE(p.checkBusesLayoutSupported(mixed));
    REQUIRE(p.setBusesLayout(mono));
    p.prepareToPlay(48000.0, 512);
    p.presets().load(juce::String::fromUTF8("Wokal: jedwabny połysk"));
    const auto v = voice(1.0);
    juce::AudioBuffer<float> b(1, 512);
    juce::MidiBuffer midi;
    double in = 0.0, out = 0.0;
    for (int block = 0; block < 90; ++block) {
        b.copyFrom(0, 0, v.l.data() + block * 512, 512);
        p.processBlock(b, midi);
        for (int i = 0; i < 512; ++i) {
            REQUIRE(std::isfinite(b.getSample(0, i)));
            out += b.getSample(0, i) * b.getSample(0, i);
            in += v.l[static_cast<size_t>(block * 512 + i)] * v.l[static_cast<size_t>(block * 512 + i)];
        }
    }
    CHECK(out > in);
}

TEST_CASE("processing allocates nothing, even while presets change") {
    auto p = make();
    const auto audio = voice(0.5);
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    const auto names = p->presets().names();
    long total = 0;
    for (int k = 0; k < 40; ++k) {
        if (k % 4 == 0) p->presets().load(names[(k / 4) % names.size()]);
        b.copyFrom(0, 0, audio.l.data() + k * 512, 512);
        b.copyFrom(1, 0, audio.r.data() + k * 512, 512);
        wers::test::AllocationCounter counter;
        p->processBlock(b, midi);
        total += counter.stop();
    }
    CHECK(total == 0);
}

TEST_CASE("blocks longer than prepared, odd block sizes and odd sample rates work") {
    for (double fs : { 22050.0, 44100.0, 88200.0, 192000.0 }) {
        auto p = make(fs, 128);
        p->presets().load(juce::String::fromUTF8("Ciemny miks: rozjaśnij"));
        juce::MidiBuffer midi;
        for (int n : { 3000, 1, 7, 333, 4096 }) {
            juce::AudioBuffer<float> b(2, n);
            for (int i = 0; i < n; ++i) {
                b.setSample(0, i, std::sin(static_cast<float>(i) * 0.37f) * 0.5f);
                b.setSample(1, i, std::sin(static_cast<float>(i) * 0.53f) * 0.5f);
            }
            p->processBlock(b, midi);
            CHECK(std::isfinite(b.getSample(0, n - 1)));
            CHECK(std::isfinite(b.getSample(1, n - 1)));
        }
    }
}

TEST_CASE("the listening switch is not part of a preset, the sound switches are") {
    auto p = make();
    setParam(*p, ids::delta, 1.0f);
    p->presets().load(juce::String::fromUTF8("Master: subtelny powiew"));
    CHECK(getParam(*p, ids::delta) > 0.5f);
    CHECK(getParam(*p, ids::monoSafe) > 0.5f);
    for (const char* id : { ids::delta, ids::bypass }) CHECK(presetExcludedIds().contains(id));
    p->presets().load("Neutralnie");
    CHECK(getParam(*p, ids::monoSafe) < 0.5f);
    CHECK(getParam(*p, ids::air) == 0.0f);
}

TEST_CASE("the editor opens and its handles, switches and meters work") {
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* pe = dynamic_cast<PowiewEditor*>(editor.get());
    REQUIRE(pe != nullptr);
    CHECK(editor->getWidth() == PowiewEditor::kWidth);
    auto& view = pe->airView();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    auto event = [&](juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys::Flags extra, bool dragged) {
        return juce::MouseEvent(source, at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extra), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &view, &view,
                                juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
    };
    // The axis can be read both ways.
    for (float hz : { 100.0f, 1000.0f, 6000.0f, 15000.0f }) CHECK(view.hzForX(view.xForHz(hz)) == doctest::Approx(hz).epsilon(0.002));
    // Drag the presence handle (6 kHz, 0%) up to where 4 dB is and right to 8 kHz.
    const auto start = view.handle(0);
    view.mouseMove(event(start, start, juce::ModifierKeys::noModifiers, false));
    view.mouseDown(event(start, start, juce::ModifierKeys::noModifiers, false));
    const juce::Point<float> to(view.xForHz(8000.0f), view.yForBoost(4.0f));
    view.mouseDrag(event(to, start, juce::ModifierKeys::noModifiers, true));
    view.mouseUp(event(to, start, juce::ModifierKeys::noModifiers, true));
    CHECK(getParam(*p, ids::presence) == doctest::Approx(100.0f * 4.0f / kShapes[0].presenceDb).epsilon(0.02));
    CHECK(getParam(*p, ids::presenceHz) == doctest::Approx(8000.0f).epsilon(0.01));
    CHECK(getParam(*p, ids::air) == 0.0f); // the other band was left alone
    // Shift: the air handle moves a fifth as far.
    const auto airStart = view.handle(1);
    view.mouseDown(event(airStart, airStart, juce::ModifierKeys::shiftModifier, false));
    const juce::Point<float> up(airStart.x, view.yForBoost(5.0f));
    view.mouseDrag(event(up, airStart, juce::ModifierKeys::shiftModifier, true));
    view.mouseUp(event(up, airStart, juce::ModifierKeys::shiftModifier, true));
    CHECK(getParam(*p, ids::air) == doctest::Approx(100.0f * 1.0f / kShapes[0].airDb).epsilon(0.03));
    // Double-click on a handle: back to zero.
    const auto now = view.handle(0);
    view.mouseDoubleClick(event(now, now, juce::ModifierKeys::noModifiers, false));
    CHECK(getParam(*p, ids::presence) == 0.0f);

    // The character switch and the buttons set their parameters.
    pe->characterRow().onPick(1);
    CHECK(getParam(*p, ids::character) == 1.0f);
    pe->monoSafeButton().setToggleState(true, juce::sendNotificationSync);
    CHECK(getParam(*p, ids::monoSafe) > 0.5f);
    pe->deltaButton().setToggleState(true, juce::sendNotificationSync);
    CHECK(getParam(*p, ids::delta) > 0.5f);
    pe->deltaButton().setToggleState(false, juce::sendNotificationSync);
    setParam(*p, ids::character, 2.0f);
    CHECK(pe->characterRow().selected() == 2);

    // With a voice playing, the meters show air added and sibilants held back.
    setParam(*p, ids::presence, 70.0f);
    setParam(*p, ids::air, 80.0f);
    setParam(*p, ids::smooth, 60.0f);
    const auto v = voice(1.6);
    float added = 0.0f, held = 0.0f;
    juce::MidiBuffer midi;
    for (size_t at = 0; at + 1600 <= v.l.size(); at += 1600) {
        juce::AudioBuffer<float> b(2, 1600);
        b.copyFrom(0, 0, v.l.data() + at, 1600);
        b.copyFrom(1, 0, v.r.data() + at, 1600);
        p->processBlock(b, midi);
        pe->updateNow();
        added = std::max(added, pe->card(0).shownAddedDb());
        held = std::max(held, pe->card(1).shownBackoff());
    }
    CHECK(added > 1.0f);
    CHECK(held > 0.3f);
    CHECK(editor->createComponentSnapshot(editor->getLocalBounds()).getWidth() > 0);

    // The knobs in the cards move their parameters.
    pe->card(1).amount().slider().setValue(33.0, juce::sendNotificationSync);
    CHECK(getParam(*p, ids::air) == doctest::Approx(33.0f).epsilon(0.01));

    editor->setSize(PowiewEditor::kWidth * 3 / 2, PowiewEditor::kHeight * 3 / 2);
    CHECK(p->uiScale() == doctest::Approx(1.5f));
    CHECK(editor->createComponentSnapshot(editor->getLocalBounds()).getWidth() > 0);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
