#include "Place.h"

namespace wers {

static constexpr std::array<float, kCutBands> kBandWeight{ 0.3f, 0.5f, 0.7f, 0.85f, 1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.5f };

float Place::liveCutDb(float vocalDb, float beatDb, int band, float depth) noexcept {
    if (vocalDb < -65.0f || depth <= 0.0f) return 0.0f;
    const float mask = beatDb - vocalDb + 3.0f;
    return depth * kBandWeight[static_cast<size_t>(band)] * std::clamp(mask * 0.7f, 0.0f, 9.0f);
}

void Place::prepare(double fs) {
    fs_ = fs;
    controlEvery_ = std::max(4, static_cast<int>(std::lround(fs / 750.0)));
    for (size_t i = 0; i < kCutBands; ++i) bell_[i].set(kCutCentres[i], kCutQ, fs);
    beatEnv_.prepare(fs);
    sideEnv_.prepare(fs);
    const double rate = fs / controlEvery_;
    att_ = static_cast<float>(onePoleCoeff(0.006, rate));
    rel_ = static_cast<float>(onePoleCoeff(0.100, rate));
    reset();
}

void Place::reset() noexcept {
    for (auto& b : bell_) b.reset();
    beatEnv_.reset(); sideEnv_.reset();
    cutDb_.fill(0.0f); gain_.fill(1.0); gainStep_.fill(0.0);
    controlCount_ = 0;
    samplesSinceSide_ = 1LL << 40;
}

void Place::updateControls(double timeSec, bool playing, const PlaceControl* control, bool haveSide) noexcept {
    bool haveVocal = false;
    if (control != nullptr && playing) haveVocal = control->vocalBands(timeSec, levels_.data());
    if (!haveVocal && haveSide) { sideEnv_.levelsDb(levels_.data()); haveVocal = true; }
    const bool haveFix = control != nullptr && playing && control->fixCuts(timeSec, fix_.data());
    beatEnv_.levelsDb(beatLevels_.data());
    const double inv = 1.0 / controlEvery_;
    for (int i = 0; i < kCutBands; ++i) {
        const auto b = static_cast<size_t>(i);
        float target = haveVocal ? liveCutDb(levels_[b], beatLevels_[b], i, depth_) : 0.0f;
        if (haveFix) target = std::max(target, fix_[b]);
        cutDb_[b] += (target - cutDb_[b]) * (target > cutDb_[b] ? att_ : rel_);
        if (target == 0.0f && cutDb_[b] < 0.005f) cutDb_[b] = 0.0f;
        const double g = cutDb_[b] == 0.0f ? 1.0 : static_cast<double>(dbToGain(-cutDb_[b]));
        gainStep_[b] = (g - gain_[b]) * inv;
    }
}

void Place::process(float* const* ch, int numChannels, int numSamples,
                    const float* const* sidechain, int numSidechain,
                    int64_t timeline, bool playing, const PlaceControl* control) noexcept {
    const int nc = std::clamp(numChannels, 0, 2);
    if (nc == 0) return;
    const bool hasSc = sidechain != nullptr && numSidechain > 0;
    for (int i = 0; i < numSamples; ++i) {
        if (controlCount_ == 0) {
            const double t = timeline >= 0 ? static_cast<double>(timeline + i) / fs_ : -1.0;
            updateControls(t, playing && timeline >= 0, control, samplesSinceSide_ < static_cast<int64_t>(fs_ * 0.25));
        }
        if (++controlCount_ >= controlEvery_) controlCount_ = 0;

        const double l = ch[0][i], r = nc > 1 ? ch[1][i] : l;
        const double mid = nc > 1 ? 0.5 * (l + r) : l;
        beatEnv_.tick(mid);
        if (hasSc) {
            double s = 0.0;
            for (int c = 0; c < numSidechain; ++c) s += sidechain[c][i];
            s /= numSidechain;
            sideEnv_.tick(s);
            samplesSinceSide_ = std::abs(s) > 1e-5 ? 0 : samplesSinceSide_ + 1;
        } else {
            samplesSinceSide_ += 1;
        }

        double y = mid;
        for (size_t b = 0; b < kCutBands; ++b) {
            gain_[b] += gainStep_[b];
            if (controlCount_ == 0) { gain_[b] = cutDb_[b] == 0.0f ? 1.0 : static_cast<double>(dbToGain(-cutDb_[b])); gainStep_[b] = 0.0; }
            const double v = bell_[b].band(y);
            if (gain_[b] != 1.0) y += (gain_[b] - 1.0) * v;
        }
        const double delta = y - mid;
        if (delta != 0.0) {
            ch[0][i] = static_cast<float>(l + delta);
            if (nc > 1) ch[1][i] = static_cast<float>(r + delta);
        }
    }
}

} // namespace wers
