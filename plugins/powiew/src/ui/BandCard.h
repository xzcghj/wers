#pragma once
#include "Knob.h"

namespace powiew {
class PowiewProcessor;
}

namespace powiew::ui {

// One band: its large amount knob, its frequency, how many dB it adds right now and how much the detector holds back.
class BandCard final : public juce::Component {
public:
    BandCard(PowiewProcessor& p, int band);
    void tick();
    void resized() override;
    void paint(juce::Graphics&) override;
    Knob& amount() noexcept { return amount_; }
    Knob& frequency() noexcept { return frequency_; }
    float shownAddedDb() const noexcept { return added_; }
    float shownBackoff() const noexcept { return backoff_; }
private:
    juce::Rectangle<float> bar(int row) const;
    PowiewProcessor& processor_;
    int band_;
    juce::Colour tint_;
    Knob amount_, frequency_;
    float added_ = 0.0f, backoff_ = 0.0f, backoffPeak_ = 0.0f;
};

} // namespace powiew::ui
