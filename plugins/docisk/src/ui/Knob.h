#pragma once
#include "DocLook.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace doc::ui {

// A rotary knob tied to one parameter, with its name above and its value below.
class Knob final : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& name, juce::Colour tint, bool bipolar = false);
    // Replaces the value text, for example with a note value in sync mode. Empty means the parameter's own text.
    std::function<juce::String()> valueOverride;
    void setTip(const juce::String& tip);
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& slider() noexcept { return slider_; }
private:
    juce::Slider slider_;
    juce::RangedAudioParameter* param_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link_;
    juce::String name_, shown_;
    juce::Colour tint_;
};

} // namespace doc::ui
