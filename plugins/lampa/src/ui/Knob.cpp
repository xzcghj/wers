#include "Knob.h"

namespace lampa::ui {

Knob::Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& name, juce::Colour tint, bool big, bool bipolar)
    : param_(state.getParameter(paramId)), name_(name), tint_(tint), big_(big) {
    jassert(param_ != nullptr);
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider_.setColour(juce::Slider::thumbColourId, tint);
    slider_.getProperties().set("bipolar", bipolar);
    slider_.getProperties().set("big", big);
    slider_.setMouseDragSensitivity(big ? 320 : 220);
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
    const auto text = param_->getCurrentValueAsText();
    if (text != shown_) {
        shown_ = text;
        repaint();
    }
}

void Knob::resized() {
    const int top = big_ ? 26 : 17, bottom = big_ ? 30 : 19;
    const int knob = juce::jmin(getWidth() - 8, getHeight() - top - bottom);
    slider_.setBounds((getWidth() - knob) / 2, top, knob, knob);
}

void Knob::paint(juce::Graphics& g) {
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    if (big_) {
        g.setFont(displayFont(700, 19.0f));
        g.setColour(colour::text.withMultipliedAlpha(alpha));
        g.drawFittedText(name_.toUpperCase(), 0, 0, getWidth(), 24, juce::Justification::centred, 1, 0.8f);
        g.setFont(uiFont(600, 16.0f));
    } else {
        g.setFont(uiFont(500, 12.5f));
        g.setColour(colour::dim.withMultipliedAlpha(alpha));
        g.drawFittedText(name_, 0, 0, getWidth(), 16, juce::Justification::centred, 1, 0.8f);
        g.setFont(uiFont(600, 13.0f));
    }
    g.setColour((slider_.isMouseOverOrDragging() ? tint_ : colour::text).withMultipliedAlpha(alpha));
    const int h = big_ ? 24 : 17;
    g.drawFittedText(shown_, 0, getHeight() - h, getWidth(), h, juce::Justification::centred, 1, 0.75f);
}

} // namespace lampa::ui
