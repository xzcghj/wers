// Tests of the whole Pogłos plugin through the entry points a host uses.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "AllocationCount.h"
#include "TestAudio.h"
#include "plugin/FactoryPresets.h"
#include "plugin/PoglosProcessor.h"
#include "ui/PoglosEditor.h"
#include <cmath>
#include <set>

WERS_COUNT_ALLOCATIONS()

using namespace pog;

namespace {

void setParam(PoglosProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

float getParam(PoglosProcessor& p, const char* id) { return p.state().getRawParameterValue(id)->load(); }

struct Run {
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Run play(PoglosProcessor& p, const wir::test::Stereo& audio, int block = 512) {
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

std::unique_ptr<PoglosProcessor> make(double fs = 48000.0, int block = 512) {
    auto p = std::make_unique<PoglosProcessor>();
    p->setPlayConfigDetails(2, 2, fs, block);
    p->prepareToPlay(fs, block);
    return p;
}

juce::MouseEvent click(juce::Component& c, juce::Point<float> at) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                            juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
}

} // namespace

TEST_CASE("there are plenty of factory presets, in groups, with unique names and descriptions") {
    const auto presets = factoryPresets();
    CHECK(presets.size() >= 50);
    std::set<juce::String> names, groups;
    for (const auto& p : presets) {
        CAPTURE(p.name);
        CHECK(names.insert(juce::String::fromUTF8(p.name)).second);
        REQUIRE(p.group != nullptr);
        REQUIRE(p.description != nullptr);
        CHECK(juce::String::fromUTF8(p.description).length() > 20);
        groups.insert(juce::String::fromUTF8(p.group));
        // Every value names a real parameter, inside its range.
        auto proc = make();
        for (const auto& [id, value] : p.values) {
            CAPTURE(id);
            auto* param = dynamic_cast<juce::RangedAudioParameter*>(proc->state().getParameter(id));
            REQUIRE(param != nullptr);
            const auto range = param->getNormalisableRange();
            CHECK(value >= range.start);
            CHECK(value <= range.end);
        }
    }
    for (const char* g : { "Wokal", "Bębny", "Gitara", "Syntezator", "Ambient", "Przestrzenie", "Płyty", "Efekty", "Miks" }) CHECK(groups.count(juce::String::fromUTF8(g)) == 1);
}

TEST_CASE("every factory preset plays cleanly and stays in a sane level range") {
    const auto audio = wir::test::demoLoop(48000.0, 6.0);
    auto ref = make();
    setParam(*ref, ids::bypass, 1.0f);
    const auto dry = play(*ref, audio);
    for (const auto& preset : factoryPresets()) {
        CAPTURE(preset.name);
        auto p = make();
        REQUIRE(p->presets().load(juce::String::fromUTF8(preset.name)));
        const auto r = play(*p, audio);
        CHECK(r.finite);
        const double db = 20.0 * std::log10(r.rms / dry.rms);
        MESSAGE(std::string(preset.name) << ": " << db << " dB, peak " << r.peak);
        CHECK(db > -12.0); // a send preset is all wet, with its low end cut
        CHECK(db < 6.0);
        CHECK(r.peak < 1.6f);
    }
}

TEST_CASE("state round trip keeps every parameter") {
    auto a = make();
    setParam(*a, ids::algo, 5.0f);
    setParam(*a, ids::era, 2.0f);
    setParam(*a, ids::decay, 7.5f);
    setParam(*a, ids::predelay, 123.0f);
    setParam(*a, ids::preSync, 1.0f);
    setParam(*a, ids::preNote, 7.0f);
    setParam(*a, ids::bassMult, 2.2f);
    setParam(*a, ids::shimPitch, 3.0f);
    setParam(*a, ids::width, 140.0f);
    juce::MemoryBlock block;
    a->getStateInformation(block);
    auto b = make();
    b->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    for (auto* param : a->getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(param);
        CAPTURE(withId->paramID);
        CHECK(getParam(*b, withId->paramID.toRawUTF8()) == doctest::Approx(getParam(*a, withId->paramID.toRawUTF8())));
    }
}

TEST_CASE("garbage state is ignored") {
    auto p = make();
    setParam(*p, ids::decay, 4.0f);
    const char junk[] = "definitely not a state";
    p->setStateInformation(junk, sizeof(junk));
    p->setStateInformation(nullptr, 0);
    std::vector<char> random(4000);
    juce::Random rnd(3);
    for (auto& c : random) c = static_cast<char>(rnd.nextInt(256));
    p->setStateInformation(random.data(), static_cast<int>(random.size()));
    CHECK(getParam(*p, ids::decay) == doctest::Approx(4.0f));
}

TEST_CASE("bypass fades to the dry signal") {
    auto p = make();
    setParam(*p, ids::mix, 100.0f);
    const auto audio = wir::test::demoLoop(48000.0, 1.0);
    play(*p, audio);
    setParam(*p, ids::bypass, 1.0f);
    juce::AudioBuffer<float> b(2, 4800);
    b.copyFrom(0, 0, audio.l.data(), 4800);
    b.copyFrom(1, 0, audio.r.data(), 4800);
    juce::MidiBuffer midi;
    p->processBlock(b, midi);
    float diff = 0.0f;
    for (int i = 1200; i < 4800; ++i) diff = std::max(diff, std::abs(b.getSample(1, i) - audio.r[static_cast<size_t>(i)]));
    CHECK(diff < 1e-6f);
}

TEST_CASE("a mono source comes out as a stereo reverb") {
    PoglosProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.checkBusesLayoutSupported(layout));
    REQUIRE(p.setBusesLayout(layout));
    p.prepareToPlay(48000.0, 512);
    p.state().getParameter(ids::mix)->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    const auto audio = wir::test::demoLoop(48000.0, 2.0);
    double side = 0.0, mid = 0.0;
    for (int block = 0; block < 180; ++block) {
        b.clear();
        b.copyFrom(0, 0, audio.l.data() + block * 512, 512);
        p.processBlock(b, midi);
        if (block > 40)
            for (int i = 0; i < 512; ++i) {
                side += std::abs(b.getSample(0, i) - b.getSample(1, i));
                mid += std::abs(b.getSample(0, i) + b.getSample(1, i));
            }
    }
    CHECK(side > 0.3 * mid);
}

TEST_CASE("predelay follows the host tempo when synced") {
    auto p = make();
    setParam(*p, ids::preSync, 1.0f);
    setParam(*p, ids::preNote, 6.0f); // 1/8
    CHECK(p->reader().read(120.0).predelayMs == doctest::Approx(250.0f));
    CHECK(p->reader().read(100.0).predelayMs == doctest::Approx(300.0f));
    setParam(*p, ids::preNote, 11.0f); // 1/2 at 40 bpm is 3 s: held at the longest
    CHECK(p->reader().read(40.0).predelayMs == doctest::Approx(kMaxPredelayMs));
    setParam(*p, ids::preSync, 0.0f);
    setParam(*p, ids::predelay, 42.0f);
    CHECK(p->reader().read(120.0).predelayMs == doctest::Approx(42.0f).epsilon(0.01));
}

TEST_CASE("processing allocates nothing, even while presets change") {
    auto p = make();
    const auto audio = wir::test::demoLoop(48000.0, 1.0);
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    const auto names = p->presets().names();
    long total = 0;
    for (int k = 0; k < 80; ++k) {
        if (k % 4 == 0) p->presets().load(names[(k / 4) % names.size()]);
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
        for (int block : { 1, 17, 3000 }) {
            auto p = make(fs, 128);
            p->presets().load(juce::String::fromUTF8("Zorza"));
            juce::AudioBuffer<float> b(2, block);
            juce::MidiBuffer midi;
            for (int round = 0; round < 20; ++round) {
                for (int i = 0; i < block; ++i) {
                    b.setSample(0, i, std::sin(static_cast<float>(i + round * block) * 0.07f) * 0.5f);
                    b.setSample(1, i, std::sin(static_cast<float>(i + round * block) * 0.05f) * 0.5f);
                }
                p->processBlock(b, midi);
            }
            CHECK(std::isfinite(b.getSample(0, block - 1)));
        }
    }
}

TEST_CASE("freeze and bypass are not part of a preset") {
    auto p = make();
    setParam(*p, ids::freeze, 1.0f);
    p->presets().load("Czysta sala");
    CHECK(getParam(*p, ids::freeze) > 0.5f);
    for (const char* id : { ids::freeze, ids::bypass }) CHECK(presetExcludedIds().contains(id));
    // Everything else is: a preset sets the algorithm back.
    setParam(*p, ids::algo, 4.0f);
    p->presets().load("Czysta sala");
    CHECK(getParam(*p, ids::algo) == 0.0f);
}

TEST_CASE("the editor opens and its pickers, buttons and displays work with the mouse") {
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* pe = dynamic_cast<PoglosEditor*>(editor.get());
    REQUIRE(pe != nullptr);
    CHECK(editor->getWidth() == PoglosEditor::kWidth);
    play(*p, wir::test::demoLoop(48000.0, 0.5));
    pe->updateNow();

    // The preset list comes in groups, and the line under the header says what the preset is for.
    {
        const auto menu = pe->presetBar().buildMenu();
        int submenus = 0, items = 0;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
            if (it.getItem().subMenu != nullptr) {
                ++submenus;
                items += it.getItem().subMenu->getNumItems();
            }
        CHECK(submenus == 10);
        CHECK(items == static_cast<int>(factoryPresets().size()));
        p->presets().load(juce::String::fromUTF8("Mgła"));
        pe->updateNow();
        CHECK(pe->shownDescription().startsWith("Ambient|"));
        CHECK(pe->shownDescription().contains(juce::String::fromUTF8("mgłę")));
    }

    // Algorithm tiles.
    auto& algos = pe->algoPicker();
    for (int i : { 2, 7, 5, 0 }) {
        const auto at = algos.tileBounds(i).getCentre().toFloat();
        algos.mouseMove(click(algos, at));
        algos.mouseDown(click(algos, at));
        CHECK(getParam(*p, ids::algo) == static_cast<float>(i));
        CHECK(algos.selected() == i);
    }
    // Era tiles.
    auto& eras = pe->eraPicker();
    for (int i : { 1, 2, 0 }) {
        const auto at = eras.tileBounds(i).getCentre().toFloat();
        eras.mouseDown(click(eras, at));
        CHECK(getParam(*p, ids::era) == static_cast<float>(i));
    }
    // Freeze.
    pe->freezeButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    CHECK(getParam(*p, ids::freeze) > 0.5f);
    pe->freezeButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    CHECK(getParam(*p, ids::freeze) < 0.5f);

    // Sync swaps the predelay knob for the note knob.
    CHECK(pe->predelayKnob().isVisible());
    pe->syncButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    pe->updateNow();
    CHECK(getParam(*p, ids::preSync) > 0.5f);
    CHECK(pe->noteKnob().isVisible());
    CHECK_FALSE(pe->predelayKnob().isVisible());

    // The shimmer controls only come alive in Błysk; its interval steps on a click.
    pe->updateNow();
    CHECK_FALSE(pe->shimmerKnob().isEnabled());
    CHECK_FALSE(pe->intervalButton().isVisible());
    setParam(*p, ids::algo, 6.0f);
    pe->updateNow();
    CHECK(pe->shimmerKnob().isEnabled());
    CHECK(pe->intervalButton().isVisible());
    pe->intervalButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    CHECK(getParam(*p, ids::shimPitch) == 1.0f);

    // Dragging the decay knob up makes the decay longer, and the display's time axis follows.
    setParam(*p, ids::algo, 0.0f);
    setParam(*p, ids::decay, 1.0f);
    pe->updateNow();
    const float before = pe->decayView().shownSeconds();
    auto& slider = pe->decayKnob().slider();
    const auto centre = slider.getLocalBounds().getCentre().toFloat();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    auto drag = [&](juce::Point<float> at, bool dragged) {
        return juce::MouseEvent(source, at, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &slider, &slider, juce::Time::getCurrentTime(), centre,
                                juce::Time::getCurrentTime(), 1, dragged);
    };
    slider.mouseDown(drag(centre, false));
    slider.mouseDrag(drag(centre + juce::Point<float>(0.0f, -120.0f), true));
    slider.mouseUp(drag(centre + juce::Point<float>(0.0f, -120.0f), true));
    CHECK(getParam(*p, ids::decay) > 3.0f);
    pe->updateNow();
    CHECK(pe->decayView().shownSeconds() > before);

    // Every algorithm and era draws, and editor sizes scale.
    for (int a = 0; a < kAlgoCount; ++a) {
        setParam(*p, ids::algo, static_cast<float>(a));
        setParam(*p, ids::era, static_cast<float>(a % kEraCount));
        play(*p, wir::test::demoLoop(48000.0, 0.2));
        pe->updateNow();
        CHECK(editor->createComponentSnapshot(editor->getLocalBounds()).getWidth() > 0);
    }
    editor->setSize(PoglosEditor::kWidth * 3 / 2, PoglosEditor::kHeight * 3 / 2);
    CHECK(p->uiScale() == doctest::Approx(1.5f));
    CHECK(editor->createComponentSnapshot(editor->getLocalBounds()).getWidth() > 0);
}

TEST_CASE("the display draws what the engine does: its curves are the measured decay") {
    Params p;
    p.decay = 3.0f;
    p.bassMult = 2.0f;
    p.dampHz = 3000.0f;
    // The middle curve falls 60 dB in about the decay time; the bass more slowly, the top faster.
    const float t0 = 0.5f, t1 = 1.5f;
    const float slopeMid = (ui::DecayView::envelopeDb(p, 1, t1) - ui::DecayView::envelopeDb(p, 1, t0)) / (t1 - t0);
    CHECK(-60.0f / slopeMid == doctest::Approx(rt60At(p, 1000.0f)).epsilon(0.01));
    const float slopeLow = (ui::DecayView::envelopeDb(p, 0, t1) - ui::DecayView::envelopeDb(p, 0, t0)) / (t1 - t0);
    const float slopeHigh = (ui::DecayView::envelopeDb(p, 2, t1) - ui::DecayView::envelopeDb(p, 2, t0)) / (t1 - t0);
    CHECK(slopeLow > slopeMid);
    CHECK(slopeHigh < slopeMid);
    // Frozen: flat. Gate: silence after its length.
    p.freeze = true;
    CHECK(ui::DecayView::envelopeDb(p, 1, 5.0f) == 0.0f);
    p.freeze = false;
    p.algo = Algo::Nonlinear;
    p.decay = 0.5f;
    CHECK(ui::DecayView::envelopeDb(p, 1, 0.8f) < -100.0f);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
