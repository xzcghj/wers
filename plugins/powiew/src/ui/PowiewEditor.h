#pragma once
#include "AirView.h"
#include "BandCard.h"
#include "Knob.h"
#include "PresetBar.h"

namespace powiew {

class PowiewProcessor;

class PowiewEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int kWidth = 1040, kHeight = 660;
    explicit PowiewEditor(PowiewProcessor& owner);
    ~PowiewEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // What the timer does, right now. For tests and pictures.
    void updateNow();
    ui::AirView& airView();
    ui::BandCard& card(int band);
    wers::ui::SegmentRow& characterRow();
    wers::ui::FlatButton& monoSafeButton();
    wers::ui::FlatButton& deltaButton();
    wers::ui::PresetBar& presetBar();
private:
    void timerCallback() override { updateNow(); }
    class Root;
    PowiewProcessor& processor_;
    ui::PowLook look_;
    std::unique_ptr<Root> root_;
};

} // namespace powiew
