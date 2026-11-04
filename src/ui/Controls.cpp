#include "Controls.h"

namespace wers::ui {

FlatButton::FlatButton(const juce::String& text, Style style, bool accentWhenOn)
    : juce::Button(text), style_(style), accent_(accentWhenOn) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle(text);
}

void FlatButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    juce::Colour fill = juce::Colours::transparentBlack, text = colour::paper, border = colour::line;
    switch (style_) {
        case Style::Outline:
            if (on) { fill = accent_ ? colour::yellow : colour::paper; text = colour::ground; border = fill; }
            else if (over) { fill = colour::paper.withAlpha(0.12f); }
            break;
        case Style::Solid:
            fill = down ? colour::yellow.darker(0.15f) : colour::yellow; text = colour::ground; border = fill;
            if (over && !down) fill = fill.brighter(0.15f);
            break;
        case Style::OnPaper:
            fill = over ? colour::ground.brighter(0.25f) : colour::ground; text = colour::paper; border = fill;
            break;
        case Style::OnPaperOutline:
            fill = over ? colour::ground.withAlpha(0.08f) : juce::Colours::transparentBlack; text = colour::ground; border = colour::ground;
            break;
        case Style::Link:
            text = over ? colour::yellow : colour::legend; border = juce::Colours::transparentBlack;
            break;
    }
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, 3.0f);
    if (border.getAlpha() != 0) {
        g.setColour(border.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(style_ == Style::OnPaperOutline ? 1.0f : 0.5f), 3.0f, style_ == Style::OnPaperOutline ? 2.0f : 1.0f);
    }
    if (hasKeyboardFocus(false)) {
        g.setColour(colour::yellow);
        g.drawRoundedRectangle(r.reduced(1.0f), 3.0f, 2.0f);
    }
    const bool big = style_ == Style::OnPaper || style_ == Style::OnPaperOutline || style_ == Style::Solid;
    g.setFont(big && textSize_ >= 18.0f ? displayFont(600, textSize_) : uiFont(500, textSize_));
    g.setColour(text.withMultipliedAlpha(alpha));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(6, 0), juce::Justification::centred, 1, 0.9f);
}

void SegmentRow::setItems(const juce::StringArray& names, const std::vector<int>& widths) {
    buttons_.clear();
    widths_ = widths;
    for (int i = 0; i < names.size(); ++i) {
        auto* b = buttons_.add(new FlatButton(names[i]));
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
    int w = 0;
    for (int v : widths_) w += v + 4;
    return w - 4;
}

void SegmentRow::resized() {
    int x = 0;
    for (int i = 0; i < buttons_.size(); ++i) {
        const int w = widths_[static_cast<size_t>(i)];
        buttons_[i]->setBounds(x, 0, w, getHeight());
        x += w + 4;
    }
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
    g.setColour(colour::paper.withMultipliedAlpha(alpha));
    g.drawText(name_, top, juce::Justification::centredLeft, true);
    g.setColour(colour::quiet.withMultipliedAlpha(alpha));
    g.drawText(juce::String(juce::roundToInt(slider.getValue())) + suffix_, top, juce::Justification::centredRight, false);
}

void ModuleHeading::paint(juce::Graphics& g) {
    g.setFont(displayFont(600, 20.0f));
    g.setColour(colour::paper);
    g.drawText(title_, 0, 0, getWidth(), 26, juce::Justification::centredLeft, false);
    g.setFont(uiFont(400, 13.0f));
    g.setColour(colour::quiet);
    g.drawFittedText(text_, 0, 26, getWidth(), getHeight() - 26, juce::Justification::topLeft, 2, 1.0f);
}

} // namespace wers::ui
