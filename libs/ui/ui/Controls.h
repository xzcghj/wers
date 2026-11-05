#pragma once
#include "Theme.h"

namespace wers::ui {

// Flat button. Solid is the one thing to press, Segment lives inside a SegmentRow.
class FlatButton : public juce::Button {
public:
    enum class Style { Outline, Solid, Segment, Link };
    explicit FlatButton(const juce::String& text, Style style = Style::Outline, bool accentWhenOn = false);
    void setStyle(Style s) { style_ = s; repaint(); }
    void setTextSize(float s) { textSize_ = s; repaint(); }
    // The look of an Outline button that is on, in place of the default.
    void setOnFill(juce::Colour fill, juce::Colour label) { onFill_ = fill; onText_ = label; hasOnFill_ = true; repaint(); }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    Style style_;
    bool accent_;
    float textSize_ = 14.0f;
    bool hasOnFill_ = false;
    juce::Colour onFill_, onText_;
};

// A row of buttons of which one is on.
class SegmentRow : public juce::Component {
public:
    std::function<void(int)> onPick;
    void setItems(const juce::StringArray& names, const std::vector<int>& widths);
    void setSelected(int index);
    int selected() const { return selected_; }
    int preferredWidth() const;
    void setTooltip(const juce::String& text);
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    juce::OwnedArray<FlatButton> buttons_;
    std::vector<int> widths_;
    int selected_ = -1;
};

// Slider with its name on the left and its value on the right, above the track.
class LabeledSlider : public juce::Component {
public:
    LabeledSlider(const juce::String& name, const juce::String& suffix = "%");
    juce::Slider slider;
    void setDescription(const juce::String&);
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    juce::String name_, suffix_;
};

// Module heading with a short description.
class ModuleHeading : public juce::Component {
public:
    void set(const juce::String& title, const juce::String& text) { title_ = title; text_ = text; repaint(); }
    void paint(juce::Graphics&) override;
private:
    juce::String title_, text_;
};

} // namespace wers::ui
