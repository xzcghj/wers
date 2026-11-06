#pragma once
// The groups on the left and the cards of the chosen group, each with its star.
#include "Favorites.h"
#include "PresetBar.h"

namespace refren {

class PresetBrowser final : public juce::Component {
public:
    PresetBrowser(wers::ui::PresetStore& store, wers::ui::PresetBar& bar, const juce::File& folder);
    ~PresetBrowser() override;
    juce::StringArray groupNames() const;
    void showGroup(const juce::String& name);
    juce::String shownGroup() const { return group_; }
    juce::StringArray shownNames() const { return shown_; }
    void choose(const juce::String& preset);
    void toggleStar(const juce::String& preset);
    bool hasStar(const juce::String& preset) const { return favorites_.contains(preset); }
    // Takes what is new, for the editor's timer.
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    class Card;
    class GroupRow;
    juce::StringArray namesIn(const juce::String& group) const;
    juce::String groupOf(const juce::String& preset) const;
    void buildCards();
    void placeCards();

    wers::ui::PresetStore& store_;
    wers::ui::PresetBar& bar_;
    Favorites favorites_;
    juce::OwnedArray<GroupRow> rows_;
    juce::Viewport viewport_;
    juce::Component holder_;
    juce::OwnedArray<Card> cards_;
    juce::String group_, lastCurrent_, emptyText_;
    juce::StringArray shown_;
};

} // namespace refren
