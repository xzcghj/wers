#pragma once
// Fonem: per-sound-class processing of a vocal. All controls at zero give a pure delay.
#include "PhonemeClassifier.h"

namespace wers {

struct FonemParams {
    float density = 0.0f;   // vowels: compression and saturation, 0..1
    float clarity = 0.0f;   // sibilants and plosive pops, 0..1
    float attack = 0.0f;    // plosive level, -1..1
    float breath = 0.0f;    // breath reduction, 0..1
    float character = 0.5f; // 0 intimate .. 1 aggressive
};

class Fonem {
public:
    static constexpr int kMaxChannels = 2;
    void prepare(double fs);
    void reset() noexcept;
    void setParams(const FonemParams& p) noexcept { params_ = p; }
    int latencySamples() const noexcept { return lookahead_ + Halfband2x::kLatency; }
    void process(float* const* ch, int numChannels, int numSamples) noexcept;
    const Memberships& memberships() const noexcept { return classifier_.current(); }
    float gainReductionDb() const noexcept { return vowelGrDb_; }
    float deEssDb() const noexcept { return deEssDb_; }
private:
    void updateControls() noexcept;
    float shape(float x) const noexcept;

    double fs_ = 48000.0;
    int lookahead_ = 192, controlEvery_ = 32, controlCount_ = 0;
    FonemParams params_;
    PhonemeClassifier classifier_;

    struct Channel {
        DelayLine look, dry;
        Svf popLp, deEss;
        Halfband2x os;
        double gain = 1.0, deEssGain = 1.0, popAmount = 0.0, satAmount = 0.0;
    };
    std::array<Channel, kMaxChannels> chan_{};

    // Control state, updated every controlEvery_ samples and ramped per sample.
    double envPower_ = 0.0, envCoef_ = 0.01;
    float vowelGrDb_ = 0.0f, makeupDb_ = 0.0f, deEssDb_ = 0.0f, centroid_ = 5000.0f;
    float grAtt_ = 0.1f, grRel_ = 0.01f, makeupCoef_ = 0.001f, deEssAtt_ = 0.3f, deEssRel_ = 0.05f;
    double gainTarget_ = 1.0, deEssTarget_ = 1.0, popTarget_ = 0.0, satTarget_ = 0.0;
    double gainStep_ = 0.0, deEssStep_ = 0.0, popStep_ = 0.0, satStep_ = 0.0;
    float prePush_ = 1.0f;
    float drive_ = 1.0f, bias_ = 0.12f, biasTanh_ = 0.0f, biasNorm_ = 1.0f;
    // Recent highest peak, and the peak about to pass through. Gain may lift a sound only up to the
    // first, and the look-ahead means the gain is already right when the second arrives.
    double peakRef_ = 0.0, peakNow_ = 0.0, peakRefCoef_ = 0.0, peakNowCoef_ = 0.0;
    int peakHold_ = 0;
    // Saturation lowers the level, so the power before and after it is compared and made up for.
    double satBefore_ = 0.0, satAfter_ = 0.0, satPowerCoef_ = 0.0;
    float satCompDb_ = 0.0f;
    int osIdleSamples_ = 0;
};

} // namespace wers
