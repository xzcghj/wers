#pragma once
#include "Modules.h"
#include "RoleViews.h"
#include "WordMapView.h"
#include "WordPanel.h"

namespace wers {

class WersEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit WersEditor(WersProcessor&);
    ~WersEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // Test hook: a state change is handled right away instead of waiting for the timer.
    void updateNow() { timerCallback(); }
    void selectWordForTests(int index) { selected_ = index; pushResult(); }
private:
    void timerCallback() override;
    void pushResult();
    void applyRoleLayout();
    void openLyricsEditor();
    Device viewDevice() const;
    int pickDefaultWord() const;

    class Root;
    WersProcessor& proc_;
    ui::WersLook look_;
    std::unique_ptr<Root> root_;
    Role shownRole_ = Role::Vocal;
    int selected_ = -1;
    uint64_t lastGeneration_ = ~0ull;
    int lastDevice_ = -1;
    std::shared_ptr<const SongResult> shownResult_;
};

} // namespace wers
