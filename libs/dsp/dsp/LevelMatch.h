#pragma once
// Follows how much louder or quieter the processed signal is and gives the gain that brings it back.
// K-weighted power in about 400 ms, within 12 dB either way, changing slowly and holding in silence.
#include "Loudness.h"

namespace wers {

class LevelMatch {
public:
    void prepare(double fs);
    void reset() noexcept;
    // Returns the linear gain to apply to wet, for this sample.
    float push(float dry, float wet) noexcept;
    // Stereo: both channels count, so sound that cancels in their sum is measured all the same.
    float push(float dryLeft, float dryRight, float wetLeft, float wetRight) noexcept;
private:
    float follow(double dryPower, double wetPower) noexcept;
    std::array<KWeighting, 2> dryWeight_, wetWeight_;
    double powerCoef_ = 0.0, gainCoef_ = 0.0;
    double dryPower_ = 0.0, wetPower_ = 0.0, gain_ = 1.0;
};

} // namespace wers
