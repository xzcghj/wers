// Renders the Pogłos editor to PNG files while demo audio plays, for reviewing the interface.
#include "TestAudio.h"
#include "plugin/PoglosProcessor.h"
#include "ui/PoglosEditor.h"

using namespace pog;

namespace {

void shot(juce::AudioProcessorEditor& editor, const juce::File& file) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(image, out);
}

void setParam(PoglosProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

void play(PoglosProcessor& p, PoglosEditor& editor, const wir::test::Stereo& audio, double seconds) {
    constexpr int kBlock = 400;
    juce::MidiBuffer midi;
    const size_t total = std::min(audio.l.size(), static_cast<size_t>(seconds * 48000.0));
    int frame = 0;
    for (size_t at = 0; at + kBlock <= total; at += kBlock) {
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
    auto audio = wir::test::demoLoop(48000.0, 8.0);
    PoglosProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 400);
    p.prepareToPlay(48000.0, 400);
    std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
    auto* editor = dynamic_cast<PoglosEditor*>(window.get());

    struct Shot {
        const char* preset;
        const char* file;
    };
    const Shot shots[] = { { "Czysta sala", "1_sala.png" }, { "Wokal: jedwabna płyta", "2_plyta.png" }, { "Kamienna katedra", "3_katedra.png" },
                           { "Werbel: bramka lat 80", "4_bramka.png" }, { "Zorza", "5_blysk.png" }, { "Betonowa komora", "6_komora.png" },
                           { "Mgła", "7_ambient.png" }, { "Mały pokój", "8_pokoj.png" }, { "Odwrócony oddech", "9_odwrocony.png" },
                           { "Arpeggio w szkle", "10_sync.png" } };
    for (const auto& s : shots) {
        p.presets().load(juce::String::fromUTF8(s.preset));
        play(p, *editor, audio, 2.5);
        shot(*window, dir.getChildFile(s.file));
    }
    p.presets().load(juce::String::fromUTF8("Pad: nieskończona sala"));
    play(p, *editor, audio, 2.0);
    setParam(p, ids::freeze, 1.0f);
    play(p, *editor, audio, 1.0);
    shot(*window, dir.getChildFile("11_freeze.png"));
    setParam(p, ids::freeze, 0.0f);

    p.presets().load(juce::String::fromUTF8("Lo-fi taśma"));
    window->setSize(PoglosEditor::kWidth * 3 / 4, PoglosEditor::kHeight * 3 / 4);
    play(p, *editor, audio, 1.0);
    shot(*window, dir.getChildFile("12_small.png"));
    return 0;
}
