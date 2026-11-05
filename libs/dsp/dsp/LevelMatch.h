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
private:
    KWeighting dryWeight_, wetWeight_;
    double powerCoef_ = 0.0, gainCoef_ = 0.0;
    double dryPower_ = 0.0, wetPower_ = 0.0, gain_ = 1.0;
};

} // namespace wers
