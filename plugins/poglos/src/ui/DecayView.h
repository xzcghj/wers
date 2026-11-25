#pragma once
#include "PogLook.h"
#include "Reverb.h"

namespace pog {
class PoglosProcessor;
}

namespace pog::ui {

// The decay as the controls set it: level against time for the low, middle and high part of the tail,
// with predelay, the early reflections, the build and the gate. Beside it, the live wet level per band.
class DecayView final : public juce::Component {
public:
    explicit DecayView(PoglosProcessor& p);
    void tick();
    void paint(juce::Graphics&) override;
    // The time axis, for tests: how many seconds the width shows.
    float shownSeconds() const noexcept { return seconds_; }
    // The level (dB, 0 at the start of the tail) the drawing shows for a band at a time.
    static float envelopeDb(const Params& p, int band, float seconds);
    static float bandHz(int band) noexcept { return band == 0 ? 120.0f : (band == 1 ? 1000.0f : 8000.0f); }
private:
    juce::Rectangle<float> plotArea() const;
    float xFor(float seconds) const;
    float yFor(float db) const;

    PoglosProcessor& processor_;
    Params shown_;
    float seconds_ = 4.0f;
    std::array<float, 3> live_{ -90.0f, -90.0f, -90.0f };
    float freeze_ = 0.0f;
};

} // namespace pog::ui
