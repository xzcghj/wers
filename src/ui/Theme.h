#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace wers::ui {

inline juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

namespace colour {
inline const juce::Colour ground{ 0xff141c6b };   // ballpoint blue
inline const juce::Colour dark{ 0xff0e1450 };
inline const juce::Colour paper{ 0xfff2f3ff };
inline const juce::Colour yellow{ 0xfff7e733 };   // highlighter: fading words
inline const juce::Colour pink{ 0xffff7ab8 };     // highlighter: dying words
inline const juce::Colour line{ 0xff7f88d8 };
inline const juce::Colour quiet{ 0xffc4cAf5 };
inline const juce::Colour legend{ 0xffd8dcfa };
inline const juce::Colour track{ 0xff2e3796 };
inline const juce::Colour meter{ 0xffc9cef5 };
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
    int getSliderThumbRadius(juce::Slider&) override { return 7; }
    juce::Font getPopupMenuFont() override { return uiFont(400, 15.0f); }
};

// Rounded rectangle with four different corner radii: the highlighter shape.
juce::Path markerShape(juce::Rectangle<float> r);

juce::String secondsToClock(double seconds);
juce::String commaNumber(float value, int decimals = 1);
juce::String frequencyLabel(double hz);

} // namespace wers::ui
