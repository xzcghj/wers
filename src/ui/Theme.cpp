#include "Theme.h"
#include <BinaryData.h>
#include <array>

namespace wers::ui {

namespace {
juce::Typeface::Ptr load(const char* data, int size) { return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size)); }

struct Faces {
    std::array<juce::Typeface::Ptr, 6> display;
    std::array<juce::Typeface::Ptr, 3> ui;
    Faces() {
        display = { load(BinaryData::Bricolage300_ttf, BinaryData::Bricolage300_ttfSize), load(BinaryData::Bricolage400_ttf, BinaryData::Bricolage400_ttfSize),
                    load(BinaryData::Bricolage500_ttf, BinaryData::Bricolage500_ttfSize), load(BinaryData::Bricolage600_ttf, BinaryData::Bricolage600_ttfSize),
                    load(BinaryData::Bricolage700_ttf, BinaryData::Bricolage700_ttfSize), load(BinaryData::Bricolage800_ttf, BinaryData::Bricolage800_ttfSize) };
        ui = { load(BinaryData::Instrument400_ttf, BinaryData::Instrument400_ttfSize), load(BinaryData::Instrument500_ttf, BinaryData::Instrument500_ttfSize),
               load(BinaryData::Instrument600_ttf, BinaryData::Instrument600_ttfSize) };
    }
};
const Faces& faces() { static const Faces f; return f; }
} // namespace

juce::Font displayFont(int weight, float size) {
    const int idx = juce::jlimit(0, 5, (weight - 300) / 100);
    return juce::Font(juce::FontOptions(faces().display[static_cast<size_t>(idx)]).withPointHeight(size));
}

juce::Font uiFont(int weight, float size) {
    const int idx = juce::jlimit(0, 2, (weight - 400) / 100);
    return juce::Font(juce::FontOptions(faces().ui[static_cast<size_t>(idx)]).withPointHeight(size));
}

WersLook::WersLook() {
    setColour(juce::PopupMenu::backgroundColourId, colour::dark);
    setColour(juce::PopupMenu::textColourId, colour::paper);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::paper);
    setColour(juce::PopupMenu::highlightedTextColourId, colour::ground);
    setColour(juce::TextEditor::backgroundColourId, colour::paper);
    setColour(juce::TextEditor::textColourId, colour::ground);
    setColour(juce::TextEditor::highlightColourId, colour::yellow);
    setColour(juce::TextEditor::highlightedTextColourId, colour::ground);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, colour::yellow);
    setColour(juce::CaretComponent::caretColourId, colour::ground);
    setColour(juce::TooltipWindow::backgroundColourId, colour::paper);
    setColour(juce::TooltipWindow::textColourId, colour::ground);
    setColour(juce::TooltipWindow::outlineColourId, colour::line);
}

void WersLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider& s) {
    const auto cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const auto left = static_cast<float>(x), right = static_cast<float>(x + width);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    g.setColour(colour::track.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(left, cy - 3.0f, right - left, 6.0f, 3.0f);
    const bool bipolar = s.getMinimum() < 0.0;
    const float zero = bipolar ? static_cast<float>(s.getPositionOfValue(0.0)) + static_cast<float>(x) * 0.0f : left;
    g.setColour(colour::yellow.withMultipliedAlpha(alpha));
    const float a = juce::jmin(zero, sliderPos), b = juce::jmax(zero, sliderPos);
    g.fillRoundedRectangle(a, cy - 3.0f, juce::jmax(0.0f, b - a), 6.0f, 3.0f);
    const float r = s.isMouseOverOrDragging() ? 8.0f : 7.0f;
    g.fillEllipse(sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
    if (s.hasKeyboardFocus(false)) {
        g.setColour(colour::paper);
        g.drawEllipse(sliderPos - r - 2.0f, cy - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f, 1.5f);
    }
}

juce::Path markerShape(juce::Rectangle<float> r) {
    const float tl = 3.0f, tr = 10.0f, br = 4.0f, bl = 9.0f;
    juce::Path p;
    p.startNewSubPath(r.getX() + tl, r.getY());
    p.lineTo(r.getRight() - tr, r.getY());
    p.quadraticTo(r.getRight(), r.getY(), r.getRight(), r.getY() + tr);
    p.lineTo(r.getRight(), r.getBottom() - br);
    p.quadraticTo(r.getRight(), r.getBottom(), r.getRight() - br, r.getBottom());
    p.lineTo(r.getX() + bl, r.getBottom());
    p.quadraticTo(r.getX(), r.getBottom(), r.getX(), r.getBottom() - bl);
    p.lineTo(r.getX(), r.getY() + tl);
    p.quadraticTo(r.getX(), r.getY(), r.getX() + tl, r.getY());
    p.closeSubPath();
    return p;
}

juce::String secondsToClock(double seconds) {
    const int total = juce::jmax(0, juce::roundToInt(seconds));
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

juce::String commaNumber(float value, int decimals) {
    // Decimal comma and a real minus sign, as in printed Polish.
    return juce::String(value, decimals).replaceCharacter('.', ',').replaceCharacter('-', juce::juce_wchar(0x2212));
}

juce::String frequencyLabel(double hz) {
    if (hz < 1000.0) return juce::String(juce::roundToInt(hz)) + " Hz";
    const auto text = commaNumber(static_cast<float>(hz / 1000.0), 1);
    return (text.endsWith(",0") ? text.dropLastCharacters(2) : text) + " kHz";
}

} // namespace wers::ui
