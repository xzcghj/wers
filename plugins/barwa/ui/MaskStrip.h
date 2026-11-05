#pragma once
#include "Controls.h"

namespace barwa {

// The last six seconds of how much colour the voice got. This is what sets the screen apart:
// the sibilants visibly get nothing.
class MaskStrip final : public juce::Component, public juce::SettableTooltipClient {
public:
    MaskStrip();
    // Called by the editor's timer. maskOn is false when the mask is turned all the way down.
    void push(float share, bool maskOn);
    int moves() const noexcept { return moves_; }
    void paint(juce::Graphics&) override;
private:
    static constexpr int kPoints = 180; // six seconds at the 30 Hz of the editor's timer
    std::array<float, kPoints> history_{};
    int next_ = 0, moves_ = 0;
    bool maskOn_ = true;
};

} // namespace barwa
