// Tests of the whole Docisk plugin through the entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "AllocationCount.h"
#include "TestAudio.h"
#include "plugin/FactoryPresets.h"
#include "plugin/DociskProcessor.h"
#include "ui/DociskEditor.h"
#include <cmath>

WERS_COUNT_ALLOCATIONS()

using namespace doc;

namespace {

void setParam(DociskProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(DociskProcessor& p, const char* id) { return p.state().getRawParameterValue(id)->load(); }

struct Run {
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

// Plays stereo audio through main in and out; a four-channel buffer carries the sidechain when it is given.
Run play(DociskProcessor& p, const wir::test::Stereo& audio, int block = 512, const wir::test::Stereo* key = nullptr, size_t skip = 0) {
    Run r;
    juce::MidiBuffer midi;
    double sum = 0.0;
    size_t count = 0;
    for (size_t at = 0; at + static_cast<size_t>(block) <= audio.l.size(); at += static_cast<size_t>(block)) {
        juce::AudioBuffer<float> b(key != nullptr ? 4 : 2, block);
        b.clear();
        b.copyFrom(0, 0, audio.l.data() + at, block);
        b.copyFrom(1, 0, audio.r.data() + at, block);
        if (key != nullptr) {
            b.copyFrom(2, 0, key->l.data() + at, block);
            b.copyFrom(3, 0, key->r.data() + at, block);
        }
        p.processBlock(b, midi);
        if (at < skip) continue;
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

std::unique_ptr<DociskProcessor> make(double fs = 48000.0, int block = 512) {
    auto p = std::make_unique<DociskProcessor>();
    p->setPlayConfigDetails(2, 2, fs, block);
    p->prepareToPlay(fs, block);
    return p;
}

// A steady four-on-the-floor kick, for the sidechain presets.
wir::test::Stereo kicks(double seconds) {
    wir::test::Stereo k{ std::vector<float>(static_cast<size_t>(48000.0 * seconds)), {} };
    for (size_t i = 0; i < k.l.size(); ++i) {
        const double t = std::fmod(static_cast<double>(i) / 48000.0, 0.5);
        k.l[i] = static_cast<float>(0.8 * std::exp(-t * 14.0) * std::sin(2.0 * 3.14159265 * (50.0 + 90.0 * std::exp(-t * 30.0)) * t));
    }
    k.r = k.l;
    return k;
}

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged, juce::ModifierKeys::Flags extra = juce::ModifierKeys::noModifiers) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extra), 0.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys::Flags extra = juce::ModifierKeys::noModifiers) {
    c.mouseMove(mouse(c, from, from, false));
    c.mouseDown(mouse(c, from, from, false, extra));
    c.mouseDrag(mouse(c, (from + to) * 0.5f, from, true, extra));
    c.mouseDrag(mouse(c, to, from, true, extra));
    c.mouseUp(mouse(c, to, from, true, extra));
}

} // namespace

TEST_CASE("there are plenty of presets, in groups, all with Polish descriptions") {
    const auto presets = factoryPresets();
    CHECK(presets.size() >= 20);
    juce::StringArray groups, names;
    for (const auto& pr : presets) {
        CHECK(pr.description != nullptr);
        CHECK(pr.group != nullptr);
        groups.addIfNotAlreadyThere(juce::String::fromUTF8(pr.group));
        names.add(juce::String::fromUTF8(pr.name));
    }
    for (const char* g : { "Wokal", "Bębny", "Bas", "Miks", "Master", "Równoległa", "Sidechain" }) CHECK(groups.contains(juce::String::fromUTF8(g)));
    names.removeDuplicates(false);
    CHECK(static_cast<size_t>(names.size()) == presets.size());
}

TEST_CASE("every factory preset plays cleanly, compresses and stays in a sane level range") {
    const auto audio = wir::test::demoLoop(48000.0, 4.0);
    const auto key = kicks(4.0);
    auto ref = make();
    setParam(*ref, ids::mix, 0.0f);
    const auto dry = play(*ref, audio, 512, nullptr, 24000);
    for (const auto& preset : factoryPresets()) {
        const std::string name(preset.name);
        CAPTURE(name);
        auto p = make();
        REQUIRE(p->presets().load(juce::String::fromUTF8(preset.name)));
        const bool sidechain = getParam(*p, ids::external) > 0.5f;
        float deepest = 0.0f;
        Run r;
        {
            // Twice over: the gain is sampled between halves.
            r = play(*p, audio, 512, sidechain ? &key : nullptr, 24000);
            deepest = Meters::take(p->compressor().meters.gainHoldDb);
        }
        CHECK(r.finite);
        const double db = 20.0 * std::log10(r.rms / dry.rms);
        MESSAGE(name << ": " << db << " dB, peak " << r.peak << ", deepest gain " << deepest << " dB");
        CHECK(db > -12.0);
        CHECK(db < 10.0);
        CHECK(r.peak < 1.6f);
        CHECK(std::abs(deepest) > 0.5f); // it does something to this music
    }
}

TEST_CASE("state round trip keeps every parameter and the editor size") {
    auto a = make();
    setParam(*a, ids::threshold, -31.5f);
    setParam(*a, ids::ratio, 6.0f);
    setParam(*a, ids::attack, 3.3f);
    setParam(*a, ids::character, 2.0f);
    setParam(*a, ids::lookahead, 4.0f);
    setParam(*a, ids::external, 1.0f);
    setParam(*a, ids::mix, 42.0f);
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
    setParam(*p, ids::threshold, -40.0f);
    const char junk[] = "definitely not a state";
    p->setStateInformation(junk, sizeof(junk));
    p->setStateInformation(nullptr, 0);
    juce::MemoryBlock block;
    p->getStateInformation(block);
    static_cast<char*>(block.getData())[14] ^= 0x55; // damage the XML
    p->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(getParam(*p, ids::threshold) == doctest::Approx(-40.0f));
}

TEST_CASE("bypass fades to the dry signal, delayed like the output") {
    auto p = make();
    setParam(*p, ids::threshold, -40.0f);
    setParam(*p, ids::ratio, 10.0f);
    setParam(*p, ids::lookahead, 2.0f);
    const auto audio = wir::test::demoLoop(48000.0, 1.0);
    play(*p, audio);
    CHECK(p->getLatencySamples() == 96);
    setParam(*p, ids::bypass, 1.0f);
    juce::AudioBuffer<float> b(2, 4800);
    b.copyFrom(0, 0, audio.l.data(), 4800);
    b.copyFrom(1, 0, audio.r.data(), 4800);
    juce::MidiBuffer midi;
    p->processBlock(b, midi);
    float diff = 0.0f;
    for (int i = 1200; i < 4800; ++i) diff = std::max(diff, std::abs(b.getSample(1, i) - audio.r[static_cast<size_t>(i - 96)]));
    CHECK(diff < 1e-6f);
}

TEST_CASE("the host is told the latency of the lookahead") {
    auto p = make();
    CHECK(p->getLatencySamples() == 0);
    setParam(*p, ids::lookahead, 5.0f);
    play(*p, wir::test::demoLoop(48000.0, 0.1));
    CHECK(p->getLatencySamples() == 240);
    auto q = make(96000.0);
    setParam(*q, ids::lookahead, 10.0f);
    q->prepareToPlay(96000.0, 512);
    CHECK(q->getLatencySamples() == 960);
}

TEST_CASE("the sidechain input reaches the detector through the host's buffer") {
    auto p = make();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p->setBusesLayout(layout));
    p->prepareToPlay(48000.0, 512);
    setParam(*p, ids::threshold, -30.0f);
    setParam(*p, ids::ratio, 8.0f);
    setParam(*p, ids::external, 1.0f);
    wir::test::Stereo quiet{ std::vector<float>(48000), std::vector<float>(48000) };
    for (size_t i = 0; i < quiet.l.size(); ++i) quiet.l[i] = quiet.r[i] = 0.02f * std::sin(static_cast<float>(i) * 0.13f);
    const auto key = kicks(1.0);
    const auto withKey = play(*p, quiet, 512, &key, 12000);
    CHECK(p->sidechainConnected());
    const auto alone = play(*make(), quiet, 512, nullptr, 12000);
    // The quiet tone is far below the threshold, yet the kick on the sidechain pulls it down.
    CHECK(20.0 * std::log10(withKey.rms / alone.rms) < -3.0);
    // Listening to the sidechain plays the kick.
    setParam(*p, ids::listen, 1.0f);
    const auto listened = play(*p, quiet, 512, &key, 12000);
    CHECK(listened.peak > 0.5f);

    // A mono input with a stereo sidechain: the key is not overwritten when the input is spread to both sides.
    DociskProcessor m;
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
    monoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(m.setBusesLayout(monoLayout));
    m.prepareToPlay(48000.0, 512);
    setParam(m, ids::threshold, -30.0f);
    setParam(m, ids::ratio, 8.0f);
    setParam(m, ids::external, 1.0f);
    juce::MidiBuffer midi;
    for (int block = 0; block < 60; ++block) {
        juce::AudioBuffer<float> b(3, 512);
        for (int i = 0; i < 512; ++i) {
            b.setSample(0, i, 0.02f * std::sin(static_cast<float>(block * 512 + i) * 0.13f));
            b.setSample(1, i, key.l[static_cast<size_t>(block * 512 + i)]);
            b.setSample(2, i, key.l[static_cast<size_t>(block * 512 + i)]);
        }
        m.processBlock(b, midi);
    }
    CHECK(Meters::take(m.compressor().meters.gainHoldDb) < -6.0f);
}

TEST_CASE("mono in and out works") {
    DociskProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.inputBuses.add(juce::AudioChannelSet::disabled());
    layout.outputBuses.add(juce::AudioChannelSet::mono());
    REQUIRE(p.checkBusesLayoutSupported(layout));
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(44100.0, 300);
    setParam(p, ids::threshold, -30.0f);
    juce::AudioBuffer<float> b(1, 300);
    juce::MidiBuffer midi;
    for (int k = 0; k < 100; ++k) {
        for (int i = 0; i < 300; ++i) b.setSample(0, i, 0.5f * std::sin(static_cast<float>(k * 300 + i) * 0.05f));
        p.processBlock(b, midi);
    }
    CHECK(std::isfinite(b.getSample(0, 299)));
    CHECK(p.compressor().currentGainDb(0) < -10.0f);
    juce::AudioProcessor::BusesLayout bad;
    bad.inputBuses.add(juce::AudioChannelSet::stereo());
    bad.inputBuses.add(juce::AudioChannelSet::disabled());
    bad.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK_FALSE(p.checkBusesLayoutSupported(bad));
}

TEST_CASE("processing allocates nothing, even while presets change") {
    auto p = make();
    const auto audio = wir::test::demoLoop(48000.0, 0.6);
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    const auto names = p->presets().names();
    long total = 0;
    for (int k = 0; k < 50; ++k) {
        if (k % 2 == 0) p->presets().load(names[(k / 2) % names.size()]);
        b.copyFrom(0, 0, audio.l.data() + k * 512, 512);
        b.copyFrom(1, 0, audio.r.data() + k * 512, 512);
        wers::test::AllocationCounter counter;
        p->processBlock(b, midi);
        total += counter.stop();
    }
    CHECK(total == 0);
}

TEST_CASE("blocks longer than prepared and odd sample rates work") {
    for (double fs : { 22050.0, 44100.0, 88200.0, 192000.0 }) {
        auto p = make(fs, 128);
        p->presets().load("Ogranicznik");
        for (int size : { 1, 3000, 77 }) {
            juce::AudioBuffer<float> b(2, size);
            for (int i = 0; i < size; ++i) {
                b.setSample(0, i, std::sin(static_cast<float>(i) * 0.07f) * 0.9f);
                b.setSample(1, i, std::sin(static_cast<float>(i) * 0.05f) * 0.9f);
            }
            juce::MidiBuffer midi;
            p->processBlock(b, midi);
            CHECK(std::isfinite(b.getSample(0, size - 1)));
        }
    }
}

TEST_CASE("listening to the sidechain is for checking, not part of a preset") {
    auto p = make();
    setParam(*p, ids::listen, 1.0f);
    REQUIRE(p->presets().load(juce::String::fromUTF8("Klej na sumę")));
    CHECK(getParam(*p, ids::listen) > 0.5f);
    CHECK(presetExcludedIds().contains(ids::listen));
    setParam(*p, ids::ratio, 20.0f);
    p->presets().load("Neutralnie");
    CHECK(getParam(*p, ids::ratio) == doctest::Approx(4.0f));
}

TEST_CASE("ratio knob: the scale runs from expansion through 1:1 to infinity and reads back") {
    CHECK(ratioFromNormalised(0.0f) == doctest::Approx(0.5f));
    CHECK(ratioFromNormalised(0.08f) == doctest::Approx(1.0f));
    CHECK(ratioFromNormalised(1.0f) == doctest::Approx(kRatioInfinite));
    for (float r : { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f, 10.0f, 50.0f }) CHECK(ratioFromNormalised(normalisedFromRatio(r)) == doctest::Approx(r).epsilon(1e-4));
    auto p = make();
    auto* ratio = p->state().getParameter(ids::ratio);
    CHECK(ratio->getText(ratio->convertTo0to1(4.0f), 20) == "4,0:1");
    CHECK(ratio->getText(1.0f, 20) == juce::String::fromUTF8("∞:1"));
    CHECK(ratio->getText(ratio->convertTo0to1(0.5f), 20) == "1:2,00");
    CHECK(ratio->convertFrom0to1(ratio->getValueForText("8:1")) == doctest::Approx(8.0f));
    CHECK(ratio->convertFrom0to1(ratio->getValueForText("inf")) == doctest::Approx(kRatioInfinite));
}

TEST_CASE("the editor opens and the curve handles can be dragged") {
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* pe = dynamic_cast<DociskEditor*>(editor.get());
    REQUIRE(pe != nullptr);
    CHECK(editor->getWidth() == DociskEditor::kWidth);
    play(*p, wir::test::demoLoop(48000.0, 0.5));
    pe->updateNow();
    auto& curve = pe->curve();

    // Threshold: from -18 to -30 dB along the input axis.
    const auto th = curve.thresholdHandle();
    CHECK(curve.handleAt(th) == ui::CurveView::Handle::Threshold);
    drag(curve, th, { curve.xForDb(-30.0f), th.y });
    CHECK(getParam(*p, ids::threshold) == doctest::Approx(-30.0f).epsilon(0.01));

    // Ratio: the right end of the curve down to where 2:1 puts it, -15 dB.
    const auto rh = curve.ratioHandle();
    CHECK(curve.handleAt(rh) == ui::CurveView::Handle::Ratio);
    drag(curve, rh, { rh.x, rh.y + (curve.yForDb(-15.0f) - curve.yForDb(-30.0f + 30.0f / 4.0f)) });
    CHECK(getParam(*p, ids::ratio) == doctest::Approx(2.0f).epsilon(0.03));
    // All the way down is infinity, up to the diagonal is 1:1.
    const auto rh2 = curve.ratioHandle();
    drag(curve, rh2, { rh2.x, curve.yForDb(-40.0f) });
    CHECK(getParam(*p, ids::ratio) == doctest::Approx(kRatioInfinite));
    const auto rh3 = curve.ratioHandle();
    drag(curve, rh3, { rh3.x, curve.yForDb(0.0f) });
    CHECK(getParam(*p, ids::ratio) == doctest::Approx(1.0f).epsilon(0.02));

    // Knee: drag the upper triangle 6 dB to the right of where it is, so the knee grows by 12 dB.
    setParam(*p, ids::knee, 0.0f);
    const auto kh = curve.kneeHandle(true);
    CHECK(curve.handleAt(kh) == ui::CurveView::Handle::Knee);
    drag(curve, kh, { curve.xForDb(-30.0f + 6.0f), kh.y });
    CHECK(getParam(*p, ids::knee) == doctest::Approx(12.0f).epsilon(0.02));
    // The lower triangle widens it too, by dragging left.
    const auto kl = curve.kneeHandle(false);
    drag(curve, kl, { kl.x - (curve.xForDb(-20.0f) - curve.xForDb(-21.0f)), kl.y });
    CHECK(getParam(*p, ids::knee) == doctest::Approx(14.0f).epsilon(0.02));

    // Shift drags five times finer; Ctrl-click resets.
    const auto th2 = curve.thresholdHandle();
    drag(curve, th2, { curve.xForDb(-20.0f), th2.y }, juce::ModifierKeys::shiftModifier);
    CHECK(getParam(*p, ids::threshold) == doctest::Approx(-28.0f).epsilon(0.01));
    const auto th3 = curve.thresholdHandle();
    curve.mouseDown(mouse(curve, th3, th3, false, juce::ModifierKeys::ctrlModifier));
    curve.mouseUp(mouse(curve, th3, th3, false, juce::ModifierKeys::ctrlModifier));
    CHECK(getParam(*p, ids::threshold) == doctest::Approx(-18.0f));
    // Empty space does nothing.
    const auto empty = juce::Point<float>(curve.xForDb(-55.0f), curve.yForDb(-5.0f));
    CHECK(curve.handleAt(empty) == ui::CurveView::Handle::None);

    // The threshold line in the history drags up and down.
    auto& hist = pe->history();
    const juce::Point<float> onLine(hist.plot().getCentreX(), hist.yForDb(-18.0f));
    drag(hist, onLine, { onLine.x, hist.yForDb(-24.0f) });
    CHECK(getParam(*p, ids::threshold) == doctest::Approx(-24.0f).epsilon(0.01));
    const juce::Point<float> away(hist.plot().getCentreX(), hist.yForDb(-50.0f));
    drag(hist, away, { away.x, hist.yForDb(-10.0f) });
    CHECK(getParam(*p, ids::threshold) == doctest::Approx(-24.0f).epsilon(0.01));

    // Ratio shortcuts.
    pe->pickRatio(4);
    CHECK(getParam(*p, ids::ratio) == doctest::Approx(kRatioInfinite));
    pe->pickRatio(1);
    CHECK(getParam(*p, ids::ratio) == doctest::Approx(2.0f));

    // The history fills from the audio, and the editor scales.
    play(*p, wir::test::demoLoop(48000.0, 1.0));
    pe->updateNow();
    CHECK(hist.columns() > 600);
    CHECK(pe->meters().shownGainDb() < -1.0f);
    editor->setSize(DociskEditor::kWidth * 3 / 2, DociskEditor::kHeight * 3 / 2);
    CHECK(p->uiScale() == doctest::Approx(1.5f));
    CHECK(editor->createComponentSnapshot(editor->getLocalBounds()).getWidth() > 0);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
