#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "analysis/Types.h"

namespace wers::ui {

inline juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

namespace colour {
inline const juce::Colour bg{ 0xff12121c };       // the room with the lights off
inline const juce::Colour surface{ 0xff1a1a27 };  // header, strips and the word panel
inline const juce::Colour raised{ 0xff262638 };   // what sits on a surface
inline const juce::Colour hairline{ 0xff34354b };
inline const juce::Colour text{ 0xffefeff7 };
inline const juce::Colour dim{ 0xffa6a8c0 };
inline const juce::Colour faint{ 0xff70728c };    // decoration and switched-off things, never for reading
// Colour always means the same thing: amber fades, coral dies, mint is clear or fixed and is what you press.
inline const juce::Colour weak{ 0xffffc04d };
inline const juce::Colour dies{ 0xffff6b8b };
inline const juce::Colour accent{ 0xff4fe3c1 };
inline const juce::Colour ink{ 0xff08201b };      // text on amber and mint
} // namespace colour

namespace layout {
inline constexpr int width = 1240, height = 780, headerH = 76, stripH = 226, panelW = 372;
} // namespace layout

// Embedded fonts. weight is one of 300, 400, 500, 600, 700, 800 for display, 400, 500, 600 for ui.
juce::Font displayFont(int weight, float size);
juce::Font uiFont(int weight, float size);

class WersLook final : public juce::LookAndFeel_V4 {
public:
    WersLook();
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 6; }
    juce::Font getPopupMenuFont() override { return uiFont(400, 15.0f); }
    void drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

// The colour of a word score: coral when it dies, amber when it fades, mint when it is clear.
juce::Colour scoreColour(float score);

juce::String secondsToClock(double seconds);
juce::String commaNumber(float value, int decimals = 1);
juce::String frequencyLabel(double hz);

} // namespace wers::ui
