#pragma once
#include "LampaLook.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace lampa::ui {

// A rotary knob tied to one parameter, with its name above and its value below. Big knobs are the two
// main controls, with a 0..10 scale and larger type.
class Knob final : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& name, juce::Colour tint, bool big = false, bool bipolar = false);
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
    bool big_;
};

} // namespace lampa::ui
