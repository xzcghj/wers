#include "LevelMatch.h"

namespace wers {

namespace {
constexpr double kPowerSeconds = 0.4;
constexpr double kGainSeconds = 0.2;
constexpr double kSilencePower = 1.0e-6;  // -60 dBFS
constexpr double kMaxGain = 3.9810717055; // +12 dB
}

void LevelMatch::prepare(double fs) {
    dryWeight_.prepare(fs);
    wetWeight_.prepare(fs);
    powerCoef_ = onePoleCoeff(kPowerSeconds, fs);
    gainCoef_ = onePoleCoeff(kGainSeconds, fs);
    reset();
}

void LevelMatch::reset() noexcept {
    dryWeight_.reset();
    wetWeight_.reset();
    dryPower_ = wetPower_ = 0.0;
    gain_ = 1.0;
}

float LevelMatch::push(float dry, float wet) noexcept {
    const double d = dryWeight_.tick(std::isfinite(dry) ? static_cast<double>(dry) : 0.0);
    const double w = wetWeight_.tick(std::isfinite(wet) ? static_cast<double>(wet) : 0.0);
    dryPower_ += (d * d - dryPower_) * powerCoef_;
    wetPower_ += (w * w - wetPower_) * powerCoef_;
    if (dryPower_ >= kSilencePower) {
        const double target = wetPower_ > 0.0 ? std::sqrt(dryPower_ / wetPower_) : kMaxGain;
        gain_ += (std::clamp(target, 1.0 / kMaxGain, kMaxGain) - gain_) * gainCoef_;
    }
    return static_cast<float>(gain_);
}

} // namespace wers
