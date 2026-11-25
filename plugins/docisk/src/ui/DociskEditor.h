#pragma once
#include "Curve.h"
#include "History.h"
#include "Knob.h"
#include "Meters.h"
#include "PresetBar.h"

namespace doc {

class DociskProcessor;

class DociskEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int kWidth = 1180, kHeight = 628;
    explicit DociskEditor(DociskProcessor& owner);
    ~DociskEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // What the timer does, right now. For tests and pictures.
    void updateNow();
    ui::CurveView& curve();
    ui::HistoryView& history();
    ui::MeterStrip& meters();
    wers::ui::PresetBar& presetBar();
    // The ratio shortcuts under the curve knobs, for tests: 1,5:1, 2, 4, 8 and infinity.
    void pickRatio(int index);
private:
    void timerCallback() override { updateNow(); }
    class Root;
    DociskProcessor& processor_;
    ui::DocLook look_;
    std::unique_ptr<Root> root_;
};

} // namespace doc
