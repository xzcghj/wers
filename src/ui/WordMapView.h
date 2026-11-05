#pragma once
#include "Controls.h"
#include "plugin/PluginProcessor.h"

namespace wers::ui {

// The main screen of the vocal role: the lyrics, with words marked by how they survive on a device.
class WordMapView final : public juce::Component {
public:
    explicit WordMapView(WersProcessor& p);
    std::function<void(int)> onSelect;
    std::function<void()> onEditLyrics;
    std::function<void(double)> onSeek;

    void refresh(const std::shared_ptr<const SongResult>& result, Device device, int selectedIndex);
    void tick(); // called by the editor timer for state, progress and following the playhead
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    int wordCount(float below, float above) const;
    static juce::String summaryText(int dying, int fading, Device device);
private:
    struct Box { int index; int line; float x, w; int weight; juce::Colour fill, text; bool underline; juce::String label; };
    void rebuild();
    int hitTest(juce::Point<int>) const;
    juce::Rectangle<float> boxBounds(const Box&) const;
    void paintWords(juce::Graphics&);
    void paintOverview(juce::Graphics&);
    juce::Rectangle<int> overviewArea() const;
    void selectAtOverview(int x);
    void showLine(int line);
    float shownScore(const Word&) const;
    void paintMessage(juce::Graphics&, const juce::String& title, const juce::String& body, float progress);
    juce::Rectangle<int> wordsArea() const;

    WersProcessor& proc_;
    std::shared_ptr<const SongResult> result_;
    Device device_ = Device::Phone;
    int selected_ = -1, hovered_ = -1, lines_ = 0, current_ = -1;
    std::vector<Box> boxes_;
    float scroll_ = 0.0f, scrollTarget_ = 0.0f;
    juce::uint32 lastManualScroll_ = 0;
    SessionPhase phase_ = SessionPhase::Idle;
    float progress_ = 0.0f;
    juce::String banner_, modelHint_;
    ModelManager::State lastModelState_ = ModelManager::State::Missing;
    int dying_ = 0, fading_ = 0, toFix_ = 0;
    bool anyFix_ = false, compare_ = false, wasFollowing_ = false;

    FlatButton listen_{ pl("Przesłuchaj numer"), FlatButton::Style::Solid };
    FlatButton finish_{ pl("Zakończ i analizuj"), FlatButton::Style::Solid };
    FlatButton cancel_{ pl("Anuluj"), FlatButton::Style::Link };
    FlatButton again_{ pl("Przesłuchaj jeszcze raz"), FlatButton::Style::Link };
    FlatButton lyrics_{ pl("Tekst numeru"), FlatButton::Style::Link };
    FlatButton fixAll_{ pl("Napraw wszystkie"), FlatButton::Style::Outline };
    FlatButton undoAll_{ pl("Cofnij wszystkie naprawy"), FlatButton::Style::Link };
    FlatButton getModel_{ pl("Pobierz model"), FlatButton::Style::Outline };
};

} // namespace wers::ui
