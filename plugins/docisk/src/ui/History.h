#pragma once
#include "DocLook.h"
#include "Compressor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace doc::ui {

// The last few seconds scrolling right to left: what came in (grey), what went out (mint) and the gain taken
// away (coral, hanging from the top on the same dB scale). The amber threshold line can be dragged.
class HistoryView final : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr float kTopDb = 0.0f, kBottomDb = -60.0f;
    HistoryView(juce::AudioProcessorValueTreeState& state, const History& history);
    // Pulls new frames from the compressor and repaints.
    void tick();
    void setNotes(const juce::String& warning) { if (warning != warning_) { warning_ = warning; repaint(); } }

    juce::Rectangle<float> plot() const;
    float yForDb(float db) const;
    float dbForY(float y) const;
    int columns() const noexcept { return static_cast<int>(frames_.size()); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    bool nearThreshold(juce::Point<float> p) const;
    juce::RangedAudioParameter& threshold_;
    const History& history_;
    std::vector<Frame> frames_, incoming_;
    size_t head_ = 0; // where the next frame goes, the oldest is there too
    uint32_t cursor_ = 0;
    bool hover_ = false, dragging_ = false;
    float dragStartDb_ = 0.0f;
    juce::String warning_;
};

} // namespace doc::ui
