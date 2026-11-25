#pragma once
#include "PogLook.h"
#include "Reverb.h"

namespace pog {
class PoglosProcessor;
}

namespace pog::ui {

// The outline of an algorithm's space, seen from above, fitted into a square inside r.
juce::Path spaceShape(Algo a, juce::Rectangle<float> r);
// Decoration that belongs to a shape: the springs of the plate, the gate of the nonlinear room and so on.
void drawShapeDetails(juce::Graphics& g, Algo a, juce::Rectangle<float> r, juce::Colour c, float alpha);

// The space from above: its outline grows with size, the early reflections are dots where they bounce,
// and rings run out from the source while the tail sounds.
class SpaceView final : public juce::Component {
public:
    explicit SpaceView(PoglosProcessor& p);
    void tick();
    void paint(juce::Graphics&) override;
private:
    PoglosProcessor& processor_;
    Params shown_;
    float level_ = 0.0f, freeze_ = 0.0f;
    double phase_ = 0.0, lastMs_ = 0.0;
};

} // namespace pog::ui
