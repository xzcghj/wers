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
    setColour(juce::PopupMenu::backgroundColourId, colour::raised);
    setColour(juce::PopupMenu::textColourId, colour::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::accent);
    setColour(juce::PopupMenu::highlightedTextColourId, colour::ink);
    setColour(juce::TextEditor::backgroundColourId, colour::surface);
    setColour(juce::TextEditor::textColourId, colour::text);
    setColour(juce::TextEditor::highlightColourId, colour::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, colour::text);
    setColour(juce::TextEditor::outlineColourId, colour::hairline);
    setColour(juce::TextEditor::focusedOutlineColourId, colour::accent);
    setColour(juce::CaretComponent::caretColourId, colour::accent);
    setColour(juce::TooltipWindow::backgroundColourId, colour::raised);
    setColour(juce::TooltipWindow::textColourId, colour::text);
    setColour(juce::TooltipWindow::outlineColourId, colour::hairline);
}

void WersLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider& s) {
    const auto cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const auto left = static_cast<float>(x), right = static_cast<float>(x + width);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;
    g.setColour(colour::hairline.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(left, cy - 2.0f, right - left, 4.0f, 2.0f);
    const float zero = s.getMinimum() < 0.0 ? static_cast<float>(s.getPositionOfValue(0.0)) : left;
    g.setColour(colour::accent.withMultipliedAlpha(alpha));
    const float a = juce::jmin(zero, sliderPos), b = juce::jmax(zero, sliderPos);
    g.fillRoundedRectangle(a, cy - 2.0f, juce::jmax(0.0f, b - a), 4.0f, 2.0f);
    const float r = s.isMouseOverOrDragging() ? 7.0f : 6.0f;
    if (s.isMouseButtonDown()) {
        g.setColour(colour::accent.withAlpha(0.25f));
        g.fillEllipse(sliderPos - r - 5.0f, cy - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
    }
    g.setColour(colour::text.withMultipliedAlpha(alpha));
    g.fillEllipse(sliderPos - r, cy - r, r * 2.0f, r * 2.0f);
    if (s.hasKeyboardFocus(false)) {
        g.setColour(colour::accent);
        g.drawEllipse(sliderPos - r - 2.0f, cy - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f, 1.5f);
    }
}

void WersLook::drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) {
    g.setColour(isMouseOver || isMouseDragging ? colour::dim : colour::faint);
    const auto right = static_cast<float>(w), bottom = static_cast<float>(h);
    for (float inset : { 4.0f, 9.0f }) g.drawLine(right - inset, bottom - 3.0f, right - 3.0f, bottom - inset, 1.2f);
}

juce::Colour scoreColour(float score) {
    return score < wers::kScoreDies ? colour::dies : (score < wers::kScoreFades ? colour::weak : colour::accent);
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
