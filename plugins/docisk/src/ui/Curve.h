#pragma once
#include "DocLook.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace doc::ui {

// The static curve, input level across and output level up, with the signal as a moving dot.
// Three handles: the threshold (on the curve), the ratio (at the right edge) and the knee (triangles under
// the curve on the input axis). Shift drags five times finer, Ctrl-click and double-click reset a handle.
class CurveView final : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr float kLowDb = -60.0f, kHighDb = 0.0f;
    enum class Handle { None, Threshold, Ratio, Knee };

    explicit CurveView(juce::AudioProcessorValueTreeState& state);
    // The detector level and the gain right now, from the meters.
    void setSignal(float levelDb, float gainDb);
    void tick();

    juce::Rectangle<float> plot() const;
    float xForDb(float db) const;
    float yForDb(float db) const;
    float dbForX(float x) const;
    float dbForY(float y) const;
    // Where each handle sits now, for drawing and for tests.
    juce::Point<float> thresholdHandle() const;
    juce::Point<float> ratioHandle() const;
    juce::Point<float> kneeHandle(bool upper) const;
    Handle handleAt(juce::Point<float> p) const;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    struct Shape { float threshold, slope, knee, range; };
    Shape shape() const;
    float outputDb(float level, const Shape& s) const;
    float plain(juce::RangedAudioParameter& p) const { return p.convertFrom0to1(p.getValue()); }
    void set(juce::RangedAudioParameter& p, float value);
    juce::RangedAudioParameter* paramFor(Handle h) const;

    juce::RangedAudioParameter &threshold_, &ratio_, &knee_, &range_;
    Handle hover_ = Handle::None, drag_ = Handle::None;
    float dragStart_ = 0.0f;
    float level_ = -120.0f, gain_ = 0.0f, shownLevel_ = -120.0f, shownGain_ = 0.0f;
    std::array<juce::Point<float>, 24> trail_{};
    size_t trailAt_ = 0;
};

} // namespace doc::ui
