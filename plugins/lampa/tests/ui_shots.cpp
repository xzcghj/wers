// Renders the Lampa editor to PNG files while demo audio plays, for reviewing the interface.
#include "LampaAudio.h"
#include "plugin/LampaProcessor.h"
#include "ui/LampaEditor.h"

using namespace lampa;

namespace {

void shot(juce::AudioProcessorEditor& editor, const juce::File& file, float scale = 1.0f) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(image, out);
}

void setParam(LampaProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// Plays audio through the plugin and moves the display along at 60 frames a second.
void play(LampaProcessor& p, LampaEditor& editor, const test::Stereo& audio, double from, double seconds) {
    constexpr int kBlock = 800; // 60 frames a second at 48 kHz
    juce::MidiBuffer midi;
    const size_t start = static_cast<size_t>(from * 48000.0);
    const size_t end = std::min(audio.l.size(), start + static_cast<size_t>(seconds * 48000.0));
    for (size_t at = start; at + kBlock <= end; at += kBlock) {
        juce::AudioBuffer<float> b(2, kBlock);
        b.copyFrom(0, 0, audio.l.data() + at, kBlock);
        b.copyFrom(1, 0, audio.r.data() + at, kBlock);
        p.processBlock(b, midi);
        editor.updateNow(1.0f / 60.0f);
    }
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("shots").getFullPathName());
    dir.createDirectory();
    const auto loop = test::materialFor("Szyna", 48000.0, 16.0);
    const auto voice = test::materialFor("Wokal", 48000.0, 8.0);
    const auto kit = test::materialFor("Bęben", 48000.0, 8.0);
    LampaProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 800);
    p.prepareToPlay(48000.0, 800);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<LampaEditor*>(window.get());

    p.presets().load(juce::String::fromUTF8("Wokal na przód"));
    play(p, *editor, voice, 0.0, 3.3);
    shot(*window, dir.getChildFile("1_wokal_redukcja.png"));
    shot(*window, dir.getChildFile("1_wokal_redukcja_2x.png"), 2.0f);

    p.setMeterMode(1);
    play(p, *editor, voice, 3.3, 2.2);
    shot(*window, dir.getChildFile("2_wokal_wyjscie.png"));
    p.setMeterMode(0);

    p.presets().load(juce::String::fromUTF8("Klej na szynę"));
    play(p, *editor, loop, 0.0, 7.0);
    shot(*window, dir.getChildFile("3_szyna_cicho_glosno.png"));

    p.presets().load(juce::String::fromUTF8("Bębny równolegle"));
    play(p, *editor, kit, 0.0, 2.6);
    shot(*window, dir.getChildFile("4_bebny_limit.png"));

    p.presets().load(juce::String::fromUTF8("Przegrzany"));
    play(p, *editor, loop, 6.0, 4.0);
    shot(*window, dir.getChildFile("5_przegrzany.png"));

    setParam(p, ids::bypass, 1.0f);
    play(p, *editor, loop, 0.0, 1.5);
    shot(*window, dir.getChildFile("6_bypass_cisza.png"));
    setParam(p, ids::bypass, 0.0f);

    p.presets().load(juce::String::fromUTF8("Wokal jedwab"));
    window->setSize(LampaEditor::kWidth * 3 / 4, LampaEditor::kHeight * 3 / 4);
    play(p, *editor, voice, 0.0, 1.5);
    shot(*window, dir.getChildFile("7_male.png"));
    return 0;
}
