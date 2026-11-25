#pragma once
#include "DocLook.h"

namespace doc::ui {

// Input (left and right), gain reduction and output (left and right), with peak hold and numbers on top.
class MeterStrip final : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr float kFloorDb = -48.0f, kMaxReductionDb = 24.0f;
    MeterStrip();
    // Peaks are linear, gain in dB (negative is reduction), clip in dB over full scale (0 when nothing was clipped).
    void update(float inL, float inR, float outL, float outR, float gainDb, float clipDb);
    // Click resets the held numbers; they also let go by themselves after three seconds without a new extreme.
    void mouseDown(const juce::MouseEvent&) override;
    void paint(juce::Graphics&) override;
    float shownGainDb() const noexcept { return gain_; }
private:
    struct Bar {
        float level = kFloorDb, hold = kFloorDb;
        int holdFrames = 0;
        void feed(float db);
    };
    std::array<Bar, 4> bars_; // in L, in R, out L, out R
    float gain_ = 0.0f, gainHold_ = 0.0f, inMax_ = -120.0f, outMax_ = -120.0f, reductionMax_ = 0.0f;
    int clipFrames_ = 0, inAge_ = 0, outAge_ = 0, reductionAge_ = 0;
};

} // namespace doc::ui
