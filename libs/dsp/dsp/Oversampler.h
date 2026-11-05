#pragma once
// Oversampling in 2x, 4x and 8x for saturation. A cascade of linear-phase halfband filters with a
// Kaiser window and about 100 dB of rejection. The round trip delay is a whole number of base samples.
// All memory lives inside the object, so nothing here allocates.
#include "Common.h"

namespace wers {

// The largest factor up to the wanted one that keeps the oversampled rate within 384 kHz.
int effectiveFactorLog2(int wanted, double fs) noexcept;

// One halfband filter, up and down. Every second tap is zero and the centre tap is 0.5, so it only
// multiplies by the side taps.
class HalfbandStage {
public:
    void prepare(int taps);
    void reset() noexcept;
    void up(float x, float& first, float& second) noexcept;
    float down(float first, float second) noexcept;
private:
    static constexpr int kMaxSide = 50;
    std::array<float, kMaxSide> coef_{};
    std::array<float, kMaxSide * 2> upHist_{}, downHist_{};
    std::array<float, kMaxSide> centreHist_{};
    int side_ = 0, upCentre_ = 0, centreDelay_ = 1;
    int upPos_ = 0, downPos_ = 0, centrePos_ = 0;
};

class Oversampler {
public:
    void prepare(int factorLog2);
    void reset() noexcept;
    int factor() const noexcept { return 1 << stages_; }
    int latencySamples() const noexcept { return latency_; }
    // One sample in, factor() samples out.
    void up(float x, float* out) noexcept;
    // factor() samples in, one sample out.
    float down(const float* in) noexcept;
private:
    std::array<HalfbandStage, 3> stage_{};
    int stages_ = 1, latency_ = 49;
    float align_ = 0.0f;
};

} // namespace wers
