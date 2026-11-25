#pragma once
#include "PogLook.h"
#include "Reverb.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace pog::ui {

// Eight tiles, one per algorithm, each with a drawing of its space. One is on.
class AlgoPicker final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit AlgoPicker(juce::AudioProcessorValueTreeState& state);
    int selected() const noexcept { return selected_; }
    // The tile at an index, for tests.
    juce::Rectangle<int> tileBounds(int index) const;
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    static juce::String tooltipFor(int index);
private:
    int indexAt(juce::Point<int> p) const;
    std::unique_ptr<juce::ParameterAttachment> link_;
    int selected_ = 0, hover_ = -1;
};

// Three eras, each a tile with a little picture of its sound.
class EraPicker final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit EraPicker(juce::AudioProcessorValueTreeState& state);
    int selected() const noexcept { return selected_; }
    juce::Rectangle<int> tileBounds(int index) const;
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    int indexAt(juce::Point<int> p) const;
    std::unique_ptr<juce::ParameterAttachment> link_;
    int selected_ = 0, hover_ = -1;
};

} // namespace pog::ui
