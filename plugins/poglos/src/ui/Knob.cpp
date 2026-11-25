#include "Knob.h"

namespace pog::ui {

Knob::Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& name, juce::Colour tint, bool bipolar)
    : param_(state.getParameter(paramId)), name_(name), tint_(tint) {
    jassert(param_ != nullptr);
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider_.setColour(juce::Slider::thumbColourId, tint);
    slider_.getProperties().set("bipolar", bipolar);
    slider_.setMouseDragSensitivity(220);
    slider_.setTitle(name);
    slider_.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible(slider_);
    link_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramId, slider_);
    slider_.setDoubleClickReturnValue(true, param_->convertFrom0to1(param_->getDefaultValue()));
    slider_.onValueChange = [this] { refresh(); };
    refresh();
}

void Knob::setTip(const juce::String& tip) { slider_.setTooltip(name_ + ": " + tip + juce::String::fromUTF8(" Dwuklik przywraca domyślne.")); }

void Knob::refresh() {
    auto text = valueOverride ? valueOverride() : juce::String();
    if (text.isEmpty()) text = param_->getCurrentValueAsText();
    if (text != shown_) {
        shown_ = text;
        repaint();
    }
}

void Knob::resized() {
    const int knob = juce::jmin(getWidth() - 8, getHeight() - 36);
    slider_.setBounds((getWidth() - knob) / 2, 17, knob, knob);
}

void Knob::paint(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    g.setFont(uiFont(500, 12.5f));
    g.setColour(colour::dim.withMultipliedAlpha(alpha));
    g.drawFittedText(name_, 0, 0, getWidth(), 16, juce::Justification::centred, 1, 0.8f);
    g.setFont(uiFont(600, 13.0f));
    g.setColour((slider_.isMouseOverOrDragging() ? tint_ : colour::text).withMultipliedAlpha(alpha));
    g.drawFittedText(shown_, 0, getHeight() - 17, getWidth(), 17, juce::Justification::centred, 1, 0.75f);
}

} // namespace pog::ui
