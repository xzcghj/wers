#pragma once
#include "Knob.h"
#include "LampaPresetBar.h"
#include "VuMeter.h"

namespace lampa {

class LampaProcessor;

class LampaEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int kWidth = 1040, kHeight = 640;
    explicit LampaEditor(LampaProcessor& owner);
    ~LampaEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // What the timer does, right now, as if dt seconds had passed. For tests and pictures.
    void updateNow(float dt = 1.0f / 60.0f);
    ui::VuMeter& meter();
    ui::LampaPresetBar& presetBar();
    ui::Knob& squeezeKnob();
    wers::ui::SegmentRow& characterRow();
    wers::ui::SegmentRow& meterRow();
    wers::ui::FlatButton& limitButton();
    juce::String description() const;
private:
    void timerCallback() override;
    class Root;
    LampaProcessor& processor_;
    ui::LampaLook look_;
    std::unique_ptr<Root> root_;
    double lastTick_ = 0.0;
};

} // namespace lampa
