#pragma once
#include "MaskStrip.h"
#include "SpectrumView.h"

namespace barwa {

class BarwaEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BarwaEditor(BarwaProcessor& owner);
    ~BarwaEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // For tests and pictures: what the timer does, right now.
    void updateNow() { timerCallback(); }
    int visibleBandColumns() const;
    SpectrumView& spectrum();
private:
    void timerCallback() override;
    class Root;
    BarwaProcessor& processor_;
    wers::ui::WersLook look_;
    std::unique_ptr<Root> root_;
};

} // namespace barwa
