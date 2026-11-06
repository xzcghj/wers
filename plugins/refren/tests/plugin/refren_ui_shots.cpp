// Renders the editor to PNG files on a synthetic voice, for reviewing the interface.
#include "TestSignals.h"
#include "plugin/RefrenProcessor.h"
#include "ui/RefrenEditor.h"

using namespace refren;

namespace {

void shot(juce::AudioProcessorEditor& editor, const juce::File& file) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    png.writeImageToStream(image, out);
}

void setParam(RefrenProcessor& p, const juce::String& id, float plain) {
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// Plays a phrase through the plugin while the editor's timer would run, at 30 frames a second.
void play(RefrenProcessor& p, RefrenEditor& editor, const std::vector<float>& voice) {
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
    juce::File dir(argc > 1 ? argv[1] : "/tmp/refren-shots");
    dir.createDirectory();
    const auto voice = wers::test::loudPhrase(48000.0, 2.0, -6.0);
    const auto folder = dir.getChildFile("presets");
    {
        RefrenProcessor p(folder);
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> window(p.createEditor());
        auto* editor = dynamic_cast<RefrenEditor*>(window.get());
        editor->browser().toggleStar(juce::String::fromUTF8("Ciasny"));
        editor->updateNow();
        shot(*window, dir.getChildFile("1_cards.png"));
        setParam(p, layerId(1, ids::on), 1.0f);
        setParam(p, layerId(1, ids::semis), 12.0f);
        setParam(p, layerId(1, ids::voices), 4.0f);
        setParam(p, layerId(1, ids::layerWidth), 100.0f);
        setParam(p, layerId(2, ids::on), 1.0f);
        setParam(p, layerId(2, ids::source), 2.0f);
        setParam(p, layerId(2, ids::semis), -12.0f);
        setParam(p, layerId(2, ids::voices), 1.0f);
        setParam(p, layerId(3, ids::on), 1.0f);
        setParam(p, layerId(3, ids::semis), 7.0f);
        setParam(p, layerId(3, ids::voices), 6.0f);
        setParam(p, layerId(3, ids::layerWidth), 80.0f);
        setParam(p, ids::verbMix, 30.0f);
        play(p, *editor, voice);
        shot(*window, dir.getChildFile("2_stage.png"));
        editor->showDetails(true);
        editor->updateNow();
        shot(*window, dir.getChildFile("3_details.png"));
        editor->showDetails(false);
        editor->browser().showGroup(juce::String::fromUTF8("Ulubione"));
        editor->browser().toggleStar(juce::String::fromUTF8("Ciasny"));
        editor->updateNow();
        shot(*window, dir.getChildFile("4_empty_group.png"));
        window.reset();
        p.setUiScale(1.5f);
        std::unique_ptr<juce::AudioProcessorEditor> larger(p.createEditor());
        dynamic_cast<RefrenEditor*>(larger.get())->browser().showGroup(juce::String::fromUTF8("Dubel"));
        dynamic_cast<RefrenEditor*>(larger.get())->updateNow();
        shot(*larger, dir.getChildFile("5_scale.png"));
    }
    folder.deleteRecursively();
    return 0;
}
