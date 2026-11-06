#include "WhisperVoice.h"

namespace refren {

namespace {
constexpr double kLowestHz = 200.0, kHighestHz = 12000.0, kBandQ = 7.0;
constexpr double kPowerSeconds = 0.002, kAttackSeconds = 0.005, kReleaseSeconds = 0.03;
constexpr double kStepSeconds = 0.00017, kLatencySeconds = 0.010;
constexpr float kInputLimit = 64.0f;
constexpr uint32_t kSeed = 22222u;
}

void WhisperVoice::prepare(double fs) {
    for (int b = 0; b < kBands; ++b) {
        const double centre = kLowestHz * std::pow(kHighestHz / kLowestHz, static_cast<double>(b) / (kBands - 1));
        auto& band = band_[static_cast<size_t>(b)];
        band.heard.set(centre, kBandQ, fs);
        band.played.set(centre, kBandQ, fs);
        // The filter lets through a part of the noise that depends on its width, which is scaled back to one.
        const double noisePower = (1.0 / 3.0) * wers::kPi * centre / (kBandQ * fs);
        band.noiseScale = static_cast<float>(1.0 / std::sqrt(noisePower));
    }
    // The level follows the power smoothed a little, or the attack would only act on the tips of each wave.
    powerCoef_ = wers::onePoleCoeff(kPowerSeconds, fs);
    step_ = std::max(1, static_cast<int>(std::lround(kStepSeconds * fs)));
    perStep_ = 1.0f / static_cast<float>(step_);
    attack_ = static_cast<float>(wers::onePoleCoeff(kAttackSeconds, fs / step_));
    release_ = static_cast<float>(wers::onePoleCoeff(kReleaseSeconds, fs / step_));
    latency_ = static_cast<int>(std::lround(kLatencySeconds * fs));
    reset();
}

void WhisperVoice::reset() noexcept {
    for (auto& band : band_) {
        band.heard.reset();
        band.played.reset();
        band.power = 0.0;
        band.level = band.shown = band.slope = 0.0f;
    }
    stepLeft_ = 0;
    seed_ = kSeed;
}

float WhisperVoice::tick(float mono) noexcept {
    const float x = std::isfinite(mono) ? std::clamp(mono, -kInputLimit, kInputLimit) : 0.0f;
    seed_ = seed_ * 1664525u + 1013904223u;
    const double noise = static_cast<double>(seed_) * (2.0 / 4294967296.0) - 1.0;
    if (--stepLeft_ < 0) {
        stepLeft_ = step_ - 1;
        for (auto& band : band_) {
            const auto wanted = static_cast<float>(std::sqrt(band.power));
            band.level += (wanted - band.level) * (wanted > band.level ? attack_ : release_);
            band.slope = (band.level - band.shown) * perStep_;
        }
    }
    double out = 0.0;
    for (auto& band : band_) {
        const double heard = band.heard.band(static_cast<double>(x));
        band.power += (heard * heard - band.power) * powerCoef_;
        band.shown += band.slope;
        out += static_cast<double>(band.shown * band.noiseScale) * band.played.band(noise);
    }
    return static_cast<float>(out);
}

} // namespace refren
