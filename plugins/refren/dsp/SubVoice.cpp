#include "SubVoice.h"

namespace refren {

namespace {
constexpr double kGlideSeconds = 0.005, kGateSeconds = 0.005;
constexpr double kSecondHarmonic = 0.3;
const double kLevelScale = 1.0 / std::sqrt(0.5 * (1.0 + kSecondHarmonic * kSecondHarmonic)); // the level of the tone is the level of the voice
}

void SubVoice::prepare(double fs, int delaySamples) {
    fs_ = fs;
    delay_ = delaySamples;
    hzCoef_ = wers::onePoleCoeff(kGlideSeconds, fs);
    gateStep_ = static_cast<float>(1.0 / (kGateSeconds * fs));
    reset();
}

void SubVoice::reset() noexcept {
    phase_ = hz_ = 0.0;
    gate_ = 0.0f;
}

float SubVoice::tick(const VoiceAnalysis& voice, float semitones) noexcept {
    const int64_t playing = voice.now() - 1 - delay_;
    const PitchReading pitch = voice.pitchAt(playing);
    if (pitch.voiced) {
        const double wanted = fs_ / static_cast<double>(pitch.period) * std::pow(2.0, static_cast<double>(semitones) / 12.0);
        // From silence it starts on the right pitch instead of gliding there.
        hz_ = gate_ <= 0.0f ? wanted : hz_ + (wanted - hz_) * hzCoef_;
        gate_ = std::min(1.0f, gate_ + gateStep_);
    } else {
        gate_ = std::max(0.0f, gate_ - gateStep_);
    }
    if (gate_ <= 0.0f) return 0.0f;
    phase_ += 2.0 * wers::kPi * hz_ / fs_;
    if (phase_ >= 2.0 * wers::kPi) phase_ -= 2.0 * wers::kPi;
    const double tone = std::sin(phase_) + kSecondHarmonic * std::sin(2.0 * phase_);
    return static_cast<float>(tone * kLevelScale) * voice.levelAt(playing) * gate_;
}

} // namespace refren
