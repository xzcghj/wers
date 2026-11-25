// Renders the Powiew editor to PNG files while audio plays, for reviewing the interface.
#include "TestAudio.h"
#include "TestVoice.h"
#include "plugin/PowiewProcessor.h"
#include "ui/PowiewEditor.h"

using namespace powiew;

namespace {

void shot(juce::AudioProcessorEditor& editor, const juce::File& file) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(image, out);
}

void setParam(PowiewProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// Plays audio through the plugin and moves the display along at 30 frames a second, stopping at "seconds".
void play(PowiewProcessor& p, PowiewEditor& editor, const wir::test::Stereo& audio, double from, double seconds) {
    constexpr int kBlock = 400;
    juce::MidiBuffer midi;
    const size_t start = static_cast<size_t>(from * 48000.0), end = std::min(audio.l.size(), static_cast<size_t>(seconds * 48000.0));
    int frame = 0;
    for (size_t at = start; at + kBlock <= end; at += kBlock) {
        juce::AudioBuffer<float> b(2, kBlock);
        b.copyFrom(0, 0, audio.l.data() + at, kBlock);
        b.copyFrom(1, 0, audio.r.data() + at, kBlock);
        p.processBlock(b, midi);
        if (++frame % 4 == 0) editor.updateNow();
    }
    editor.updateNow();
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("shots").getFullPathName());
    dir.createDirectory();
    const auto v = powiew::test::voiceLoop(48000.0, 8.0);
    const wir::test::Stereo voice{ v.l, v.r };
    const auto loop = wir::test::demoLoop(48000.0, 8.0);
    PowiewProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 400);
    p.prepareToPlay(48000.0, 400);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<PowiewEditor*>(window.get());

    p.presets().load("Neutralnie");
    play(p, *editor, voice, 0.0, 1.0);
    shot(*window, dir.getChildFile("1_neutralnie.png"));

    // In the middle of a vowel: the air goes through.
    p.presets().load(juce::String::fromUTF8("Wokal: jedwabny połysk"));
    play(p, *editor, voice, 0.0, 1.6 + 0.35);
    shot(*window, dir.getChildFile("2_wokal_samogloska.png"));

    // On an "s": the detector holds the boost back.
    play(p, *editor, voice, 1.95, 1.6 + 1.6 + 0.08);
    shot(*window, dir.getChildFile("3_wokal_sybilant.png"));

    p.presets().load(juce::String::fromUTF8("Ciemny miks: rozjaśnij"));
    play(p, *editor, loop, 0.0, 2.6);
    shot(*window, dir.getChildFile("4_ciemny_miks_analog.png"));

    p.presets().load(juce::String::fromUTF8("Blachy: połysk"));
    setParam(p, ids::delta, 1.0f);
    play(p, *editor, loop, 2.6, 4.6);
    shot(*window, dir.getChildFile("5_blachy_sam_dodatek.png"));
    setParam(p, ids::delta, 0.0f);

    p.presets().load(juce::String::fromUTF8("Master: drogi połysk"));
    play(p, *editor, loop, 4.6, 6.6);
    shot(*window, dir.getChildFile("6_master.png"));

    window->setSize(PowiewEditor::kWidth * 3 / 4, PowiewEditor::kHeight * 3 / 4);
    play(p, *editor, loop, 6.6, 7.6);
    shot(*window, dir.getChildFile("7_small.png"));
    return 0;
}
