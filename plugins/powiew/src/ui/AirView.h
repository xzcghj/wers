#pragma once
#include "PowLook.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace powiew {
class PowiewProcessor;
}

namespace powiew::ui {

// The big display. Behind: the spectrum of the output, and in colour the spectrum of what Powiew adds to it.
// In front: the boost curve, dashed as the knobs set it and solid as the detector lets it through right now,
// with a handle for each band: drag sideways for the frequency, up and down for the amount.
class AirView final : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr float kFloorDb = -96.0f, kTopDb = -6.0f, kCurveTopDb = 12.0f;

    explicit AirView(PowiewProcessor& p);
    // Reads the audio and moves the display on. The editor's timer calls it.
    void tick();

    // The frequency axis is logarithmic with a share of linear, so the top octaves, where Powiew works, get room.
    float xForHz(float hz) const;
    float hzForX(float x) const;
    float yForBoost(float db) const;
    float boostForY(float y) const;
    juce::Point<float> handle(int band) const; // 0 presence, 1 air

    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(20.0f); }
    float maxDb(int band) const;
    int handleAt(juce::Point<float> at) const;
    void setFromMouse(int band, juce::Point<float> at, bool fine);
    juce::Path spectrumPath(const std::vector<float>& dbs, const juce::Rectangle<float>& area, bool closed) const;

    PowiewProcessor& processor_;
    juce::dsp::FFT fft_{ kFftOrder };
    std::vector<float> window_, out_, add_, outFft_, addFft_, outDb_, addDb_, addShown_;
    int hover_ = -1, dragging_ = -1;
    juce::Point<float> dragStart_;
    float startAmount_ = 0.0f, startHz_ = 0.0f;
};

} // namespace powiew::ui
