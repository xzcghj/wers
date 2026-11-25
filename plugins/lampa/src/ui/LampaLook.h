#pragma once
#include "Theme.h"

namespace lampa::ui {

using namespace wers::ui;

// The warm side of the WERS colours: amber is the tube, the rest stays the series' night palette.
namespace warm {
inline const juce::Colour amber{ 0xffffc04d };    // colour::weak: the glow of the tube
inline const juce::Colour ember{ 0xffff8a3d };    // hotter
inline const juce::Colour face{ 0xfff4e4c1 };     // the paper of the meter
inline const juce::Colour faceGlow{ 0xffffd88f };
inline const juce::Colour ink{ 0xff2a2118 };      // print on the meter
inline const juce::Colour panel{ 0xff1d1a24 };    // a slightly warm surface
inline const juce::Colour panelEdge{ 0xff3a3342 };
} // namespace warm

// Knobs: dark bakelite with a ring of grip, an amber arc for the value. Big knobs get a 0..10 scale.
class LampaLook final : public WersLook {
public:
    LampaLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

} // namespace lampa::ui
