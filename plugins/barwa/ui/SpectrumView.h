#pragma once
#include "Controls.h"
#include "Fft.h"
#include "plugin/BarwaProcessor.h"
#include <complex>

namespace barwa {

// Input and output on one frequency scale, with what the plugin adds filled in, and the handles of the
// band splits. Audio reaches this view through the processor's lock-free queue.
class SpectrumView final : public juce::Component {
public:
    explicit SpectrumView(BarwaProcessor& processor);
    // Called by the editor's timer.
    void refresh(int bands);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    int handleCount() const noexcept { return bands_ - 1; }
    float handleX(int index) const;
private:
    static constexpr int kFftSize = 2048;
    static constexpr int kPoints = 256;
    float frequencyToX(double hz) const;
    double xToFrequency(float x) const;
    float levelToY(float db) const;
    double topFrequency() const;
    int handleAt(float x) const;
    void analyse();
    void paintGrid(juce::Graphics&) const;
    void paintHandles(juce::Graphics&) const;

    BarwaProcessor& processor_;
    std::array<juce::RangedAudioParameter*, 2> split_{};
    int bands_ = 1, hovered_ = -1, dragged_ = -1;

    wers::Fft fft_{ kFftSize };
    std::vector<float> window_;
    std::vector<float> inHistory_, outHistory_;
    std::vector<std::complex<double>> spectrum_;
    std::vector<double> inPower_, outPower_;
    std::array<float, kPoints> shownIn_{}, shownOut_{};
    juce::uint32 overUntil_ = 0;
};

} // namespace barwa
