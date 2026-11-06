#pragma once
#include "LayerColumn.h"
#include "PresetBrowser.h"
#include "StageView.h"

namespace refren {

class RefrenEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit RefrenEditor(RefrenProcessor& owner);
    ~RefrenEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // For tests and pictures: what the timer does, right now.
    void updateNow() { timerCallback(); }
    PresetBrowser& browser();
    StageView& stage();
    wers::ui::PresetBar& presetBar();
    LayerColumn& layerColumn(int layer);
    void showDetails(bool shown);
    bool detailsShown() const;
    juce::StringArray idsInDetails() const;
private:
    void timerCallback() override;
    class Root;
    RefrenProcessor& processor_;
    wers::ui::WersLook look_;
    std::unique_ptr<Root> root_;
};

} // namespace refren
