#pragma once
#include "Theme.h"

namespace powiew::ui {

using namespace wers::ui;

// Each band keeps one colour everywhere: its handle in the display, its knobs and its meters.
// Presence is warm, air is cold; what the detector holds back is amber, as fading is everywhere in WERS.
namespace hue {
inline const juce::Colour presence{ 0xffffb38a };
inline const juce::Colour air{ 0xff8fdcff };
} // namespace hue

// Rotary knobs keep their colour in the slider's thumbColourId, are bipolar when the property says so and get a
// soft halo when they are one of the two large ones.
class PowLook final : public WersLook {
public:
    PowLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

} // namespace powiew::ui
