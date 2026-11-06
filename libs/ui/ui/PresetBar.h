#pragma once
#include "Controls.h"
#include "PresetStore.h"

namespace wers::ui {

// Previous, name, next, save, and the two slots to compare. The store does the work.
class PresetBar final : public juce::Component, private juce::TextEditor::Listener {
public:
    explicit PresetBar(PresetStore& store);
    void step(int direction);
    // The arrows walk only over these. Until this is called they walk over all presets.
    void setWalkList(const juce::StringArray& names);
    void pickSlot(int index);
    void copyToOtherSlot();
    void saveAs(const juce::String& name);
    void loadNamed(const juce::String& name);
    juce::String shownName() const { return name_.getButtonText(); }
    // What saving offers as the name, and which preset the menu can delete now. Empty when there is none.
    juce::String nameToOffer() const;
    juce::String nameToDelete() const;
    // Shows a message in place of the name for a few seconds.
    void showError(const juce::String& message);
    // Called by the editor's timer, for changes that come from a loaded project.
    void refresh();
    void resized() override;
private:
    void openMenu();
    void startNaming();
    void stopNaming();
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;
    void textEditorFocusLost(juce::TextEditor&) override;

    PresetStore& store_;
    FlatButton previous_{ "<" }, name_{ "" }, next_{ ">" }, save_{ "" }, slotA_{ "A", FlatButton::Style::Segment }, slotB_{ "B", FlatButton::Style::Segment }, copy_{ "" };
    juce::TextEditor naming_;
    juce::String error_;
    std::optional<juce::StringArray> walkList_;
    juce::uint32 errorUntil_ = 0;
};

} // namespace wers::ui
