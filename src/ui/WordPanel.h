#pragma once
#include "Controls.h"
#include "plugin/PluginProcessor.h"

namespace wers::ui {

// The page on the right: one word, why it suffers and what the fix does.
class WordPanel final : public juce::Component {
public:
    explicit WordPanel(WersProcessor& p);
    void refresh(const std::shared_ptr<const SongResult>& result, Device device, int selectedIndex);
    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void paintDevices(juce::Graphics&, juce::Rectangle<int> area, const Word&) const;
    juce::String statusLine(const Word&) const;
    juce::String causeLine(const Word&) const;
    static juce::String vocalFixLine(const WordFix&);
    static juce::String beatFixLine(const WordFix&, double seconds);

    WersProcessor& proc_;
    std::shared_ptr<const SongResult> result_;
    Device device_ = Device::Phone;
    int index_ = -1;
    bool compare_ = false;
    SongSession::Preview preview_;
    FlatButton fix_{ pl("Napraw słowo"), FlatButton::Style::Solid };
    FlatButton undo_{ pl("Cofnij naprawę"), FlatButton::Style::Outline };
};

} // namespace wers::ui
