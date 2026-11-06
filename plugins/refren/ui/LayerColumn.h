#pragma once
// The controls of the details view: one column for each layer, and the pieces the rest of the sound is made of.
#include "Controls.h"
#include "plugin/RefrenProcessor.h"

namespace refren {

// A slider that says what it is and how much in the words of its parameter.
class ParamSlider final : public juce::Component {
public:
    ParamSlider(juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name);
    const juce::String id;
    juce::Slider slider;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    juce::RangedAudioParameter& parameter_;
    juce::String name_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link_;
};

// A choice of a few names. Its button shows the present one and opens the list.
class ChoiceRow final : public juce::Component {
public:
    ChoiceRow(juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name);
    const juce::String id;
    // Runs after a person picked something.
    std::function<void(int)> onPicked;
    // Runs when the value changes, whoever changed it.
    std::function<void(int)> onChanged;
    void pick(int index);
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    juce::AudioParameterChoice& parameter_;
    juce::String name_;
    wers::ui::FlatButton button_{ "" };
    juce::ParameterAttachment link_;
};

class LayerColumn final : public juce::Component {
public:
    LayerColumn(RefrenProcessor& processor, int layer);
    juce::StringArray idsShown() const;
    // What a person picking a source does.
    void pickSource(int index);
    bool isDimmed(const char* suffix) const;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    RefrenProcessor& processor_;
    const int layer_;
    wers::ui::FlatButton on_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onLink_;
    ChoiceRow source_;
    juce::OwnedArray<ParamSlider> sliders_;
    Source shownSource_ = Source::Voice;
    void updateDimming();
};

} // namespace refren
