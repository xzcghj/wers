#include "Controls.h"

namespace wers::ui {

FlatButton::FlatButton(const juce::String& text, Style style, bool accentWhenOn)
    : juce::Button(text), style_(style), accent_(accentWhenOn) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle(text);
}

void FlatButton::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    const float radius = getHeight() >= 44 ? 8.0f : 6.0f;
    juce::Colour fill = juce::Colours::transparentBlack, text = colour::text, border = juce::Colours::transparentBlack;
    switch (style_) {
        case Style::Outline:
            border = colour::hairline;
            if (on) { fill = accent_ ? colour::weak : colour::raised; text = accent_ ? colour::ink : colour::text; border = fill; }
            else if (over) { fill = colour::raised; }
            break;
        case Style::Solid:
            fill = down ? colour::accent.darker(0.15f) : (over ? colour::accent.brighter(0.12f) : colour::accent);
            text = colour::ink;
            break;
        case Style::Segment:
            text = on || over ? colour::text : colour::dim;
            if (on) fill = colour::raised;
            break;
        case Style::Link:
            text = over ? colour::text : colour::dim;
            break;
    }
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, radius);
    if (border.getAlpha() != 0) {
        g.setColour(border.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
    if (hasKeyboardFocus(false)) {
        g.setColour(colour::accent);
        g.drawRoundedRectangle(r.reduced(1.0f), radius, 1.5f);
    }
    g.setFont(style_ == Style::Solid && textSize_ >= 18.0f ? displayFont(600, textSize_) : uiFont(500, textSize_));
    g.setColour(text.withMultipliedAlpha(alpha));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(6, 0), juce::Justification::centred, 1, 0.9f);
}

void SegmentRow::setItems(const juce::StringArray& names, const std::vector<int>& widths) {
    buttons_.clear();
    widths_ = widths;
    for (int i = 0; i < names.size(); ++i) {
        auto* b = buttons_.add(new FlatButton(names[i], FlatButton::Style::Segment));
        b->onClick = [this, i] { if (onPick) onPick(i); };
        addAndMakeVisible(b);
    }
    resized();
}

void SegmentRow::setSelected(int index) {
    selected_ = index;
    for (int i = 0; i < buttons_.size(); ++i) buttons_[i]->setToggleState(i == index, juce::dontSendNotification);
}

int SegmentRow::preferredWidth() const {
    int w = 6;
    for (int v : widths_) w += v + 2;
    return w - 2;
}

void SegmentRow::resized() {
    int x = 3;
    for (int i = 0; i < buttons_.size(); ++i) {
        const int w = widths_[static_cast<size_t>(i)];
        buttons_[i]->setBounds(x, 3, w, getHeight() - 6);
        x += w + 2;
    }
}

void SegmentRow::paint(juce::Graphics& g) {
    g.setColour(colour::bg);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
}

LabeledSlider::LabeledSlider(const juce::String& name, const juce::String& suffix) : name_(name), suffix_(suffix) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setTitle(name);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(slider);
}

void LabeledSlider::resized() { slider.setBounds(getLocalBounds().withTrimmedTop(20).withHeight(28)); }

void LabeledSlider::paint(juce::Graphics& g) {
    auto top = getLocalBounds().removeFromTop(20);
    const float alpha = slider.isEnabled() ? 1.0f : 0.5f;
    g.setFont(uiFont(400, 13.0f));
    g.setColour(colour::dim.withMultipliedAlpha(alpha));
    g.drawText(name_, top, juce::Justification::centredLeft, true);
    g.setFont(uiFont(600, 13.0f));
    g.setColour(colour::text.withMultipliedAlpha(alpha));
    g.drawText(juce::String(juce::roundToInt(slider.getValue())) + suffix_, top, juce::Justification::centredRight, false);
}

void ModuleHeading::paint(juce::Graphics& g) {
    g.setFont(displayFont(600, 18.0f));
    g.setColour(colour::text);
    g.drawText(title_, 0, 0, getWidth(), 26, juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 13.0f));
    g.setColour(colour::dim);
    g.drawFittedText(text_, 0, 26, getWidth(), getHeight() - 26, juce::Justification::topLeft, 2, 1.0f);
}

} // namespace wers::ui
