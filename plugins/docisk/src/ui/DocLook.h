#pragma once
#include "Theme.h"

namespace doc::ui {

using namespace wers::ui;

// Colour means the same everywhere: grey is what comes in, mint what goes out, coral the gain taken away,
// amber the threshold and anything that warns.
namespace hue {
inline const juce::Colour input{ 0xff8a8db0 };
inline const juce::Colour output = colour::accent;
inline const juce::Colour reduction = colour::dies;
inline const juce::Colour threshold = colour::weak;
inline const juce::Colour key{ 0xffa78bfa }; // the sidechain
} // namespace hue

// Rotary knobs keep their colour in the slider's thumbColourId and are bipolar when the property says so.
class DocLook final : public WersLook {
public:
    DocLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

} // namespace doc::ui
