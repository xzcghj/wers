#pragma once
#include "LampaLook.h"
#include "Needle.h"

namespace lampa::ui {

// The big analog meter: a backlit paper face, a needle with the mechanics of a real VU meter, and two
// pilot lamps, one for how hot the tube runs and one for the memory of the gain cell.
class VuMeter final : public juce::Component, public juce::SettableTooltipClient {
public:
    enum class Mode { Reduction = 0, Output = 1 };

    VuMeter();
    // A click on the face flips what the needle shows.
    std::function<void()> onFlip;

    void setMode(Mode m);
    Mode mode() const { return mode_; }
    // Feeds the needle: reduction in dB, output as a linear RMS-like level, heat 0..1+, memory in dB.
    void update(float reductionDb, float outputLevel, float heat, float memoryDb, float dtSeconds);
    float needlePosition() const { return needle_.position; }
    // Angle of a deflection 0..1, in radians from straight up.
    static float angleFor(float deflection);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    void drawFace(juce::Graphics&);
    juce::Point<float> pivot() const;
    float arcRadius() const;

    Mode mode_ = Mode::Reduction;
    Needle needle_;
    float heat_ = 0.0f, memory_ = 0.0f, reduction_ = 0.0f, outputVu_ = -60.0f;
    juce::Rectangle<float> face_;
};

} // namespace lampa::ui
