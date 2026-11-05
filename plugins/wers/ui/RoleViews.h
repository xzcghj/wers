#pragma once
#include "Controls.h"
#include "plugin/PluginProcessor.h"

namespace wers::ui {

// Beat role: one depth control, the bands being cut and the state of the connection.
class BeatView final : public juce::Component {
public:
    explicit BeatView(WersProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    WersProcessor& proc_;
    LabeledSlider depth_{ pl("Głębokość cięcia") };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthA_;
    std::array<float, kCutBands> cuts_{};
    juce::String status_;
};

// Master role: loudness numbers and the listening simulation.
class MasterView final : public juce::Component {
public:
    explicit MasterView(WersProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    WersProcessor& proc_;
    SegmentRow norm_;
    FlatButton reset_{ pl("Mierz od nowa"), FlatButton::Style::Link };
    void paintHistory(juce::Graphics&, juce::Rectangle<float> area) const;
    WersProcessor::MeterValues values_{};
    int deviceIndex_ = 4, normIndex_ = 0;
    // Short-term loudness over the song. When full, every second point goes, so the song always fits.
    std::vector<float> history_;
    double historyStep_ = 0.5, historyNext_ = 0.0;
};

} // namespace wers::ui
