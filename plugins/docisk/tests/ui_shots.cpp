// Renders the Docisk editor to PNG files while demo audio plays, for reviewing the interface.
#include "TestKeys.h"
#include "plugin/DociskProcessor.h"
#include "ui/DociskEditor.h"

using namespace doc;

namespace {

void shot(juce::AudioProcessorEditor& editor, const juce::File& file) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(image, out);
}

void setParam(DociskProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// Plays audio through the plugin, with the sidechain when given, and moves the display along at 30 frames a second.
void play(DociskProcessor& p, DociskEditor& editor, const wir::test::Stereo& audio, double seconds, const wir::test::Stereo* key = nullptr) {
    constexpr int kBlock = 400;
    static size_t at = 0;
    juce::MidiBuffer midi;
    const size_t total = static_cast<size_t>(seconds * 48000.0);
    int frame = 0;
    for (size_t done = 0; done + kBlock <= total; done += kBlock) {
        if (at + kBlock > audio.l.size()) at = 0;
        juce::AudioBuffer<float> b(4, kBlock);
        b.clear();
        b.copyFrom(0, 0, audio.l.data() + at, kBlock);
        b.copyFrom(1, 0, audio.r.data() + at, kBlock);
        if (key != nullptr) {
            b.copyFrom(2, 0, key->l.data() + at, kBlock);
            b.copyFrom(3, 0, key->r.data() + at, kBlock);
        }
        p.processBlock(b, midi);
        at += kBlock;
        if (++frame % 4 == 0) editor.updateNow();
    }
    editor.updateNow();
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("shots").getFullPathName());
    dir.createDirectory();
    const auto audio = wir::test::demoLoop(48000.0, 16.0);
    const auto kicks = doc::test::kickTrack(48000.0, 16.0);
    DociskProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(layout);
    p.prepareToPlay(48000.0, 400);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<DociskEditor*>(window.get());

    p.presets().load("Neutralnie");
    play(p, *editor, audio, 4.0);
    shot(*window, dir.getChildFile("1_neutralnie.png"));

    p.presets().load(juce::String::fromUTF8("Wokal: gęsto"));
    play(p, *editor, audio, 4.0);
    shot(*window, dir.getChildFile("2_wokal_gesto.png"));

    p.presets().load(juce::String::fromUTF8("Master: przezroczyście"));
    play(p, *editor, audio, 4.0);
    shot(*window, dir.getChildFile("3_master_klej.png"));

    p.presets().load(juce::String::fromUTF8("Nowojorska na bębny"));
    play(p, *editor, audio, 4.0);
    shot(*window, dir.getChildFile("4_rownolegla.png"));

    p.presets().load("Pompowanie od stopy");
    play(p, *editor, audio, 4.0, &kicks);
    shot(*window, dir.getChildFile("5_sidechain.png"));

    // External chosen but nothing connected, and listening to the key.
    setParam(p, ids::listen, 1.0f);
    play(p, *editor, audio, 2.0);
    shot(*window, dir.getChildFile("6_brak_sc_odsluch.png"));
    setParam(p, ids::listen, 0.0f);

    // Expansion, with the threshold handle held by the mouse so its value shows.
    p.presets().load(juce::String::fromUTF8("Bębny: więcej życia"));
    play(p, *editor, audio, 3.0);
    {
        auto& curve = editor->curve();
        const auto h = curve.thresholdHandle();
        const auto e = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), h, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                        &curve, &curve, juce::Time::getCurrentTime(), h, juce::Time::getCurrentTime(), 1, false);
        curve.mouseMove(e);
        curve.mouseDown(e);
        curve.repaint();
        shot(*window, dir.getChildFile("7_ekspansja_przeciaganie.png"));
        curve.mouseUp(e);
    }

    p.presets().load("Ogranicznik");
    window->setSize(DociskEditor::kWidth * 3 / 4, DociskEditor::kHeight * 3 / 4);
    play(p, *editor, audio, 3.0);
    shot(*window, dir.getChildFile("8_maly_ogranicznik.png"));

    window->setSize(DociskEditor::kWidth, DociskEditor::kHeight);
    p.presets().load(juce::String::fromUTF8("Klej na sumę"));
    play(p, *editor, audio, 4.0);
    shot(*window, dir.getChildFile("9_klej.png"));
    return 0;
}
