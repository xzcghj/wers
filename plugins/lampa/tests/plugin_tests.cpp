// Tests of the whole Lampa plugin through the entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "AllocationCount.h"
#include "LampaAudio.h"
#include "plugin/FactoryPresets.h"
#include "plugin/LampaProcessor.h"
#include "ui/LampaEditor.h"
#include <cmath>

WERS_COUNT_ALLOCATIONS()

using namespace lampa;

namespace {

void setParam(LampaProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(LampaProcessor& p, const char* id) { return p.state().getRawParameterValue(id)->load(); }

struct Run {
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Run play(LampaProcessor& p, const test::Stereo& audio, int block = 512) {
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
            ++count;
        }
    }
    r.rms = std::sqrt(sum / static_cast<double>(std::max<size_t>(1, count)));
    return r;
}

std::unique_ptr<LampaProcessor> make(double fs = 48000.0, int block = 512) {
    auto p = std::make_unique<LampaProcessor>();
    p->setPlayConfigDetails(2, 2, fs, block);
    p->prepareToPlay(fs, block);
    return p;
}


} // namespace

TEST_CASE("every factory preset plays cleanly, stays near the input level and keeps its group's material in hand") {
    const auto presets = factoryPresets();
    CHECK(presets.size() >= 20);
    juce::StringArray groups;
    for (const auto& preset : presets) {
        const std::string presetName = preset.name;
        CAPTURE(presetName);
        groups.addIfNotAlreadyThere(juce::String::fromUTF8(preset.group));
        CHECK(juce::String::fromUTF8(preset.description).length() > 20);
        const auto audio = test::materialFor(preset.group, 48000.0, 6.0);
        auto ref = make();
        setParam(*ref, ids::bypass, 1.0f);
        const auto dry = play(*ref, audio);
        auto p = make();
        REQUIRE(p->presets().load(juce::String::fromUTF8(preset.name)));
        const auto r = play(*p, audio);
        CHECK(r.finite);
        const double db = 20.0 * std::log10(r.rms / dry.rms);
         MESSAGE(presetName << ": " << db << " dB, peak " << r.peak);
        CHECK(db > -4.0);
        CHECK(db < 4.0);
        CHECK(r.peak < 1.12f);
    }
    for (const char* g : { "Wokal", "Bas", "Gitara", "Bęben", "Szyna", "Master", "Kolor" }) CHECK(groups.contains(juce::String::fromUTF8(g)));
}

TEST_CASE("it really levels: the swing between the quiet and the loud part shrinks") {
    // A vocal sings its verse 12 dB quieter than its chorus. With a vocal preset the jump gets much smaller.
    auto audio = test::normalised(test::vocal(48000.0, 8.0), -4.0);
    for (size_t i = 0; i < 4 * 48000; ++i) {
        audio.l[i] *= 0.25f;
        audio.r[i] *= 0.25f;
    }
    auto p = make();
    REQUIRE(p->presets().load(juce::String::fromUTF8("Wokal na przód")));
    juce::AudioBuffer<float> b(2, static_cast<int>(audio.l.size()));
    b.copyFrom(0, 0, audio.l.data(), b.getNumSamples());
    b.copyFrom(1, 0, audio.r.data(), b.getNumSamples());
    juce::MidiBuffer midi;
    for (int at = 0; at < b.getNumSamples(); at += 512) {
        juce::AudioBuffer<float> view(b.getArrayOfWritePointers(), 2, at, juce::jmin(512, b.getNumSamples() - at));
        p->processBlock(view, midi);
    }
    auto level = [](const float* x, int from, int to) {
        double s = 0.0;
        for (int i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
        return 10.0 * std::log10(s / (to - from));
    };
    const double inSwing = level(audio.l.data(), 5 * 48000, 8 * 48000) - level(audio.l.data(), 1 * 48000, 4 * 48000);
    const double outSwing = level(b.getReadPointer(0), 5 * 48000, 8 * 48000) - level(b.getReadPointer(0), 1 * 48000, 4 * 48000);
    MESSAGE("verse to chorus: " << inSwing << " dB in, " << outSwing << " dB out");
    CHECK(outSwing < inSwing - 3.0);
    // On the drums, parallel squeezing brings up the body between the hits against the hits themselves.
    const auto kit = test::normalised(test::drums(48000.0, 4.0), -3.0);
    auto q = make();
    REQUIRE(q->presets().load(juce::String::fromUTF8("Bębny równolegle")));
    juce::AudioBuffer<float> d(2, static_cast<int>(kit.l.size()));
    d.copyFrom(0, 0, kit.l.data(), d.getNumSamples());
    d.copyFrom(1, 0, kit.r.data(), d.getNumSamples());
    for (int at = 0; at < d.getNumSamples(); at += 512) {
        juce::AudioBuffer<float> view(d.getArrayOfWritePointers(), 2, at, juce::jmin(512, d.getNumSamples() - at));
        q->processBlock(view, midi);
    }
    const int lag = q->getLatencySamples();
    auto bodyToHit = [](const float* x, int offset) {
        double body = 0.0, hit = 0.0;
        for (int beat = 2; beat < 7; ++beat) {
            const int start = beat * 24000 + offset;
            for (int i = 0; i < 1440; ++i) hit = std::max(hit, static_cast<double>(std::abs(x[start + i])));
            double s = 0.0;
            for (int i = 7200; i < 21600; ++i) s += static_cast<double>(x[start + i]) * x[start + i];
            body += std::sqrt(s / 14400.0);
        }
        return 20.0 * std::log10(body / 5.0 / hit);
    };
    const double before = bodyToHit(kit.l.data(), 0), after = bodyToHit(d.getReadPointer(0), lag);
    MESSAGE("drum body against the hit: " << before << " dB dry, " << after << " dB through");
    CHECK(after > before + 2.0);
}

TEST_CASE("the plugin reports the oversampler's latency") {
    auto p = make(48000.0);
    CHECK(p->getLatencySamples() == p->engine().latency());
    CHECK(p->getLatencySamples() > 0);
    auto q = make(96000.0);
    CHECK(q->getLatencySamples() == q->engine().latency());
}

TEST_CASE("state round trip keeps every parameter and the meter mode") {
    auto a = make();
    setParam(*a, ids::squeeze, 63.0f);
    setParam(*a, ids::gain, 7.5f);
    setParam(*a, ids::color, 44.0f);
    setParam(*a, ids::mix, 70.0f);
    setParam(*a, ids::mode, 1.0f);
    setParam(*a, ids::limit, 1.0f);
    setParam(*a, ids::link, 30.0f);
    setParam(*a, ids::sidechain, 150.0f);
    setParam(*a, ids::output, -2.5f);
    a->setMeterMode(1);
    juce::MemoryBlock block;
    a->getStateInformation(block);
    auto b = make();
    b->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    for (auto* param : a->getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(param);
        CAPTURE(withId->paramID);
        CHECK(getParam(*b, withId->paramID.toRawUTF8()) == doctest::Approx(getParam(*a, withId->paramID.toRawUTF8())));
    }
    CHECK(b->meterMode() == 1);
}

TEST_CASE("garbage state is ignored") {
    auto p = make();
    setParam(*p, ids::squeeze, 50.0f);
    const char junk[] = "definitely not a state";
    p->setStateInformation(junk, sizeof(junk));
    p->setStateInformation(nullptr, 0);
    juce::MemoryBlock wrong;
    {
        juce::MemoryOutputStream out(wrong, false);
        out.writeInt(0x4c414d31);
        out.writeInt(1);
        out.writeInt(100000);
    }
    p->setStateInformation(wrong.getData(), static_cast<int>(wrong.getSize()));
    CHECK(getParam(*p, ids::squeeze) == doctest::Approx(50.0f));
}

TEST_CASE("bypass fades to the dry signal, delayed by the latency") {
    auto p = make();
    REQUIRE(p->presets().load(juce::String::fromUTF8("Przegrzany")));
    const auto audio = test::dynamicLoop(48000.0, 1.0);
    play(*p, audio);
    setParam(*p, ids::bypass, 1.0f);
    juce::AudioBuffer<float> b(2, 9600);
    b.copyFrom(0, 0, audio.l.data(), 9600);
    b.copyFrom(1, 0, audio.r.data(), 9600);
    juce::MidiBuffer midi;
    p->processBlock(b, midi);
    const int d = p->getLatencySamples();
    float diff = 0.0f;
    for (int i = 2400; i < 9600; ++i) diff = std::max(diff, std::abs(b.getSample(1, i) - audio.r[static_cast<size_t>(i - d)]));
    CHECK(diff < 1e-6f);
    // The host's own bypass does the same.
    auto q = make();
    juce::AudioBuffer<float> c(2, 9600);
    c.copyFrom(0, 0, audio.l.data(), 9600);
    c.copyFrom(1, 0, audio.l.data(), 9600);
    q->processBlockBypassed(c, midi);
    diff = 0.0f;
    for (int i = 2400; i < 9600; ++i) diff = std::max(diff, std::abs(c.getSample(0, i) - audio.l[static_cast<size_t>(i - d)]));
    CHECK(diff < 1e-6f);
}

TEST_CASE("mono in, mono out works") {
    LampaProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.outputBuses.add(juce::AudioChannelSet::mono());
    REQUIRE(p.checkBusesLayoutSupported(layout));
    REQUIRE(p.setBusesLayout(layout));
    juce::AudioProcessor::BusesLayout odd;
    odd.inputBuses.add(juce::AudioChannelSet::mono());
    odd.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK_FALSE(p.checkBusesLayoutSupported(odd));
    p.prepareToPlay(48000.0, 700);
    p.presets().load("Bas gruby");
    const auto audio = test::bassLine(48000.0, 2.0);
    juce::AudioBuffer<float> b(1, 700);
    juce::MidiBuffer midi;
    double sum = 0.0;
    for (int block = 0; block < 130; ++block) {
        b.copyFrom(0, 0, audio.l.data() + block * 700, 700);
        p.processBlock(b, midi);
        for (int i = 0; i < 700; ++i) {
            REQUIRE(std::isfinite(b.getSample(0, i)));
            sum += std::abs(b.getSample(0, i));
        }
    }
    CHECK(sum > 100.0);
    CHECK(p.engine().meterReductionDb() > 0.5f);
}

TEST_CASE("processing allocates nothing, even while presets change") {
    auto p = make();
    const auto audio = test::dynamicLoop(48000.0, 0.5);
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    const auto names = p->presets().names();
    long total = 0;
    for (int k = 0; k < 44; ++k) {
        if (k % 2 == 0) p->presets().load(names[(k / 2) % names.size()]);
        b.copyFrom(0, 0, audio.l.data() + k * 512, 512);
        b.copyFrom(1, 0, audio.r.data() + k * 512, 512);
        wers::test::AllocationCounter counter;
        p->processBlock(b, midi);
        total += counter.stop();
    }
    CHECK(total == 0);
}

TEST_CASE("blocks longer than prepared, odd lengths and odd sample rates work") {
    for (double fs : { 22050.0, 44100.0, 88200.0, 192000.0 }) {
        auto p = make(fs, 128);
        p->presets().load("Wokal radiowy");
        juce::MidiBuffer midi;
        for (int len : { 3000, 1, 17, 513 }) {
            juce::AudioBuffer<float> b(2, len);
            for (int i = 0; i < len; ++i) {
                b.setSample(0, i, std::sin(static_cast<float>(i) * 0.07f) * 0.9f);
                b.setSample(1, i, std::sin(static_cast<float>(i) * 0.05f) * 0.9f);
            }
            p->processBlock(b, midi);
            CHECK(std::isfinite(b.getSample(0, len - 1)));
            CHECK(std::isfinite(b.getSample(1, len - 1)));
        }
    }
}

TEST_CASE("the editor opens, the meter flips with a click, the knobs and switches do what they say") {
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* le = dynamic_cast<LampaEditor*>(editor.get());
    REQUIRE(le != nullptr);
    CHECK(editor->getWidth() == LampaEditor::kWidth);
    setParam(*p, ids::squeeze, 70.0f);
    play(*p, test::dynamicLoop(48000.0, 3.0));
    for (int i = 0; i < 40; ++i) le->updateNow(1.0f / 60.0f);
    // In reduction mode the needle has left the 0 VU mark towards the left.
    CHECK(p->meterMode() == 0);
    CHECK(le->meter().needlePosition() < vuDeflection(-0.5f));

    // A click on the meter face flips it to the output level, and back.
    auto& meter = le->meter();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> c(meter.getWidth() * 0.5f, meter.getHeight() * 0.4f);
    auto click = [&](juce::Component& target, juce::Point<float> at) {
        const auto now = juce::Time::getCurrentTime();
        target.mouseDown(juce::MouseEvent(source, at, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, at, now, 1, false));
        target.mouseUp(juce::MouseEvent(source, at, juce::ModifierKeys::noModifiers, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, at, now, 1, false));
    };
    click(meter, c);
    le->updateNow();
    CHECK(p->meterMode() == 1);
    CHECK(meter.mode() == ui::VuMeter::Mode::Output);
    CHECK(le->meterRow().selected() == 1);
    click(meter, c);
    le->updateNow();
    CHECK(p->meterMode() == 0);

    // Dragging the big knob up turns Ściskanie up.
    auto& s = le->squeezeKnob().slider();
    const float before = getParam(*p, ids::squeeze);
    const juce::Point<float> mid(s.getWidth() * 0.5f, s.getHeight() * 0.5f), up(mid.x, mid.y - 60.0f);
    const auto now = juce::Time::getCurrentTime();
    s.mouseDown(juce::MouseEvent(source, mid, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &s, &s, now, mid, now, 1, false));
    s.mouseDrag(juce::MouseEvent(source, up, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &s, &s, now, mid, now, 1, true));
    s.mouseUp(juce::MouseEvent(source, up, juce::ModifierKeys::noModifiers, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &s, &s, now, mid, now, 1, true));
    CHECK(getParam(*p, ids::squeeze) > before + 5.0f);

    // The character buttons and the Limit switch set their parameters.
    auto* tube = dynamic_cast<juce::Button*>(le->characterRow().getChildComponent(1));
    REQUIRE(tube != nullptr);
    tube->onClick();
    CHECK(getParam(*p, ids::mode) == 1.0f);
    le->limitButton().setToggleState(true, juce::sendNotificationSync);
    CHECK(getParam(*p, ids::limit) > 0.5f);

    // A preset shows its description.
    p->presets().load("Bas gruby");
    le->updateNow();
    CHECK(le->description().contains("harmonicznych"));

    editor->setSize(LampaEditor::kWidth * 3 / 2, LampaEditor::kHeight * 3 / 2);
    CHECK(p->uiScale() == doctest::Approx(1.5f));
    CHECK(editor->createComponentSnapshot(editor->getLocalBounds()).getWidth() > 0);
}

TEST_CASE("bypass is not part of a preset") {
    auto p = make();
    setParam(*p, ids::bypass, 1.0f);
    p->presets().load(juce::String::fromUTF8("Klej na szynę"));
    CHECK(getParam(*p, ids::bypass) > 0.5f);
    CHECK(presetExcludedIds().contains(ids::bypass));
    setParam(*p, ids::color, 90.0f);
    p->presets().load(juce::String::fromUTF8("Domyślny"));
    CHECK(getParam(*p, ids::color) == doctest::Approx(25.0f));
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
