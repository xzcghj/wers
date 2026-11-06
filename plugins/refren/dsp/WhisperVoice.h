#pragma once
// The same words without a pitch: the level of the voice in 16 bands leads noise in the same bands.
#include "Filters.h"

namespace refren {

class WhisperVoice {
public:
    void prepare(double fs);
    void reset() noexcept;
    float tick(float mono) noexcept;
    // How long the stream takes to start after the voice does.
    int latencySamples() const noexcept { return latency_; }
private:
    static constexpr int kBands = 16;
    struct Band {
        wers::Svf heard, played;
        double power = 0.0;
        float level = 0.0f, shown = 0.0f, slope = 0.0f, noiseScale = 1.0f;
    };
    std::array<Band, kBands> band_{};
    double powerCoef_ = 0.01;
    float attack_ = 0.03f, release_ = 0.005f, perStep_ = 0.125f;
    int step_ = 8, stepLeft_ = 0, latency_ = 144;
    uint32_t seed_ = 1u;
};

} // namespace refren
