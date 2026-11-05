#pragma once
#include "Controls.h"
#include "plugin/PluginProcessor.h"

namespace wers::ui {

// Character pad: dot moves freely, the four corners are named styles.
class CharacterPad final : public juce::Component {
public:
    explicit CharacterPad(WersProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void tick();
private:
    void setFromPoint(juce::Point<int>);
    WersProcessor& proc_;
    juce::RangedAudioParameter* x_, *y_;
    float lastX_ = -1.0f, lastY_ = -1.0f;
    FlatButton gloss_{ "Gloss", FlatButton::Style::Link }, rage_{ "Rage", FlatButton::Style::Link }, szept_{ "Szept", FlatButton::Style::Link }, drill_{ "Drill", FlatButton::Style::Link };
};

// The strip below the words in the vocal role.
class ModuleStrip final : public juce::Component {
public:
    explicit ModuleStrip(WersProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    WersProcessor& proc_;
    ModuleHeading fonemHead_, placeHead_, spaceHead_, charHead_;
    LabeledSlider density_{ pl("Samogłoski: gęstość") }, clarity_{ pl("Syczące: czystość") }, attack_{ pl("Zwarte: atak") }, breath_{ pl("Oddechy: ciszej") };
    LabeledSlider place_{ pl("Głębokość cięcia") }, space_{ pl("Ilość efektów") };
    std::unique_ptr<Attachment> densityA_, clarityA_, attackA_, breathA_, spaceA_;
    CharacterPad pad_;
};

} // namespace wers::ui
