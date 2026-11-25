#pragma once
#include "Theme.h"

namespace pog::ui {

using namespace wers::ui;

// Each part of the spectrum keeps one colour everywhere: its decay curve, its meter and its knobs.
namespace hue {
inline const juce::Colour low{ 0xffa78bfa };   // the bass of the tail
inline const juce::Colour mid{ 0xff4fe3c1 };   // the middle, what the decay control sets
inline const juce::Colour high{ 0xffffc04d };  // the top of the tail
inline const juce::Colour space{ 0xff4cc9f0 }; // the room: size, time, reflections
inline const juce::Colour ice{ 0xff9fdcff };   // frozen
juce::Colour forBand(int band);
} // namespace hue

// Rotary knobs keep their colour in the slider's thumbColourId and are bipolar when the property says so.
class PogLook final : public WersLook {
public:
    PogLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

} // namespace pog::ui
