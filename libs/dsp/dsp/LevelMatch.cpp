#include "LevelMatch.h"

namespace wers {

namespace {
constexpr double kPowerSeconds = 0.4;
constexpr double kGainSeconds = 0.2;
constexpr double kSilencePower = 1.0e-6;  // -60 dBFS
constexpr double kMaxGain = 3.9810717055; // +12 dB

double clean(float x) noexcept { return std::isfinite(x) ? static_cast<double>(x) : 0.0; }
}

void LevelMatch::prepare(double fs) {
    for (auto& w : dryWeight_) w.prepare(fs);
    for (auto& w : wetWeight_) w.prepare(fs);
    powerCoef_ = onePoleCoeff(kPowerSeconds, fs);
    gainCoef_ = onePoleCoeff(kGainSeconds, fs);
    reset();
}

void LevelMatch::reset() noexcept {
    for (auto& w : dryWeight_) w.reset();
    for (auto& w : wetWeight_) w.reset();
    dryPower_ = wetPower_ = 0.0;
    gain_ = 1.0;
}

float LevelMatch::follow(double dryPower, double wetPower) noexcept {
    dryPower_ += (dryPower - dryPower_) * powerCoef_;
    wetPower_ += (wetPower - wetPower_) * powerCoef_;
    if (dryPower_ >= kSilencePower) {
        const double target = wetPower_ > 0.0 ? std::sqrt(dryPower_ / wetPower_) : kMaxGain;
        gain_ += (std::clamp(target, 1.0 / kMaxGain, kMaxGain) - gain_) * gainCoef_;
    }
    return static_cast<float>(gain_);
}

float LevelMatch::push(float dry, float wet) noexcept {
    const double d = dryWeight_[0].tick(clean(dry));
    const double w = wetWeight_[0].tick(clean(wet));
    return follow(d * d, w * w);
}

float LevelMatch::push(float dryLeft, float dryRight, float wetLeft, float wetRight) noexcept {
    const double dl = dryWeight_[0].tick(clean(dryLeft)), dr = dryWeight_[1].tick(clean(dryRight));
    const double wl = wetWeight_[0].tick(clean(wetLeft)), wr = wetWeight_[1].tick(clean(wetRight));
    return follow(0.5 * (dl * dl + dr * dr), 0.5 * (wl * wl + wr * wr));
}

} // namespace wers
