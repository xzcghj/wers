#pragma once
#include "DecayView.h"
#include "Knob.h"
#include "Pickers.h"
#include "PogPresetBar.h"
#include "SpaceView.h"

namespace pog {

class PoglosProcessor;

class PoglosEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int kWidth = 1120, kHeight = 756;
    explicit PoglosEditor(PoglosProcessor& owner);
    ~PoglosEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // What the timer does, right now. For tests and pictures.
    void updateNow();
    ui::AlgoPicker& algoPicker();
    ui::EraPicker& eraPicker();
    ui::DecayView& decayView();
    ui::PresetBar& presetBar();
    // The group and description shown under the header, for tests.
    juce::String shownDescription() const;
    juce::Button& freezeButton();
    juce::Button& syncButton();
    juce::Button& intervalButton();
    ui::Knob& predelayKnob();
    ui::Knob& noteKnob();
    ui::Knob& shimmerKnob();
    ui::Knob& decayKnob();
private:
    void timerCallback() override { updateNow(); }
    class Root;
    PoglosProcessor& processor_;
    ui::PogLook look_;
    std::unique_ptr<Root> root_;
};

} // namespace pog
