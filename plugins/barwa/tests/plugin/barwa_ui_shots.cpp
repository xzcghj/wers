// Renders the editor to PNG files on a synthetic voice, for reviewing the interface.
#include "plugin/BarwaProcessor.h"
#include "TestSignals.h"
#include "ui/BarwaEditor.h"

using namespace barwa;

namespace {

void shot(juce::AudioProcessorEditor& editor, const juce::File& file) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(image, out);
}

void setParam(BarwaProcessor& p, const char* id, float plain) {
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// Plays a phrase through the plugin while the editor's timer would run, at 30 frames a second.
void play(BarwaProcessor& p, BarwaEditor& editor, const std::vector<float>& voice) {
    constexpr int kBlock = 512;
    juce::MidiBuffer midi;
    int sinceFrame = 0;
    for (size_t from = 0; from + kBlock <= voice.size(); from += kBlock) {
        juce::AudioBuffer<float> block(2, kBlock);
        block.copyFrom(0, 0, voice.data() + from, kBlock);
        block.copyFrom(1, 0, voice.data() + from, kBlock);
        p.processBlock(block, midi);
        sinceFrame += kBlock;
        if (sinceFrame >= 1600) {
            sinceFrame = 0;
            editor.updateNow();
        }
    }
    editor.updateNow();
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir(argc > 1 ? argv[1] : "/tmp/barwa-shots");
    dir.createDirectory();
    const auto voice = wers::test::loudPhrase(48000.0, 7.0, -6.0);
    {
        BarwaProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
        auto* editor = dynamic_cast<BarwaEditor*>(window.get());
        setParam(p, ids::drive1, 14.0f);
        editor->updateNow();
        shot(*window, dir.getChildFile("1_empty.png"));
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("2_one_band.png"));
        setParam(p, ids::bands, 1.0f);
        setParam(p, ids::drive2, 8.0f);
        setParam(p, ids::char2, 1.0f);
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("3_two_bands.png"));
        setParam(p, ids::bands, 2.0f);
        setParam(p, ids::drive3, 6.0f);
        setParam(p, ids::char1, 2.0f);
        setParam(p, ids::char3, 3.0f);
        setParam(p, ids::tone, -30.0f);
        setParam(p, ids::solo2, 0.0f);
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("4_three_bands.png"));
        setParam(p, ids::mask, 0.0f);
        setParam(p, ids::autoLevel, 0.0f);
        setParam(p, ids::delta, 1.0f);
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("5_delta.png"));
        setParam(p, ids::delta, 0.0f);
        setParam(p, ids::output, 12.0f);
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("6_overload.png"));
        setParam(p, ids::output, 0.0f);
        editor->presetBar().saveAs(juce::String::fromUTF8("Czysty start"));
        editor->updateNow();
        shot(*window, dir.getChildFile("7_preset_error.png"));
        p.presets().load(juce::String::fromUTF8("Delikatny klej na master"));
        editor->updateNow();
        juce::Thread::sleep(4100);
        editor->updateNow();
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("8_preset_loaded.png"));
        window.reset();
    }
    return 0;
}
