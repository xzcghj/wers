#include "VocalFix.h"

namespace wers {

void VocalFix::prepare(double fs) {
    fs_ = fs;
    controlEvery_ = std::max(4, static_cast<int>(std::lround(fs / 1500.0)));
    for (auto& c : bell_)
        for (size_t j = 0; j < kBoostBands; ++j) c[j].set(kBoostCentres[j], kBoostQ, fs);
    reset();
}

void VocalFix::reset() noexcept {
    for (auto& c : bell_) for (auto& b : c) b.reset();
    gain_.fill(1.0); step_.fill(0.0); target_.fill(1.0);
    controlCount_ = 0;
}

void VocalFix::process(float* const* ch, int numChannels, int numSamples, int64_t timeline, bool playing, const Plan* plan, bool enabled) noexcept {
    const int nc = std::clamp(numChannels, 0, 2);
    if (nc == 0) return;
    const bool active = playing && enabled && plan != nullptr && timeline >= 0;
    std::array<float, kBoostBands> boost{};
    for (int i = 0; i < numSamples; ++i) {
        if (controlCount_ == 0) {
            bool any = false;
            if (active) any = plan->vocalBoostAt(static_cast<double>(timeline + i) / fs_, boost.data());
            for (size_t j = 0; j < kBoostBands; ++j) {
                const double t = any && boost[j] > 0.0f ? static_cast<double>(dbToGain(boost[j])) : 1.0;
                step_[j] = (t - gain_[j]) / controlEvery_;
                target_[j] = t;
            }
        }
        if (++controlCount_ >= controlEvery_) controlCount_ = 0;
        for (size_t j = 0; j < kBoostBands; ++j) {
            gain_[j] += step_[j];
            if (controlCount_ == 0) gain_[j] = target_[j];
        }
        for (int c = 0; c < nc; ++c) {
            double y = ch[c][i];
            for (size_t j = 0; j < kBoostBands; ++j) {
                const double b = bell_[static_cast<size_t>(c)][j].band(y);
                if (gain_[j] != 1.0) y += (gain_[j] - 1.0) * b;
            }
            ch[c][i] = static_cast<float>(y);
        }
    }
}

void StaleDetector::prepare(double) { reset(); }

void StaleDetector::reset() noexcept {
    fill_ = 0; lastFrame_ = -1; badWindows_ = 0; stale_ = false;
}

void StaleDetector::feed(double t, float levelDb, const Plan* plan) noexcept {
    if (plan == nullptr || plan->levelTrack.empty() || t < 0.0) return;
    const int f = frameAtTime(t);
    if (f < 0 || f >= static_cast<int>(plan->levelTrack.size())) return;
    if (f == lastFrame_) return;
    if (lastFrame_ >= 0 && (f < lastFrame_ || f > lastFrame_ + 20)) fill_ = 0; // a jump: start a new window
    lastFrame_ = f;
    live_[static_cast<size_t>(fill_)] = levelDb;
    stored_[static_cast<size_t>(fill_)] = plan->levelTrack[static_cast<size_t>(f)];
    if (++fill_ >= kWindow) { evaluate(); fill_ = 0; }
}

void StaleDetector::evaluate() noexcept {
    // Levels are clamped to the top 45 dB of the window so silence does not dominate the comparison.
    float maxL = -200.0f, maxS = -200.0f;
    for (int i = 0; i < kWindow; ++i) { maxL = std::max(maxL, live_[static_cast<size_t>(i)]); maxS = std::max(maxS, stored_[static_cast<size_t>(i)]); }
    const bool liveSilent = maxL < -70.0f, storedSilent = maxS < -70.0f;
    if (liveSilent && storedSilent) return;            // nothing to compare
    double corr = 0.0;
    if (liveSilent != storedSilent) {
        corr = -1.0;                                   // one has voice where the other has none
    } else {
        double sa = 0, sb = 0;
        for (int i = 0; i < kWindow; ++i) { sa += std::max(live_[static_cast<size_t>(i)], maxL - 45.0f); sb += std::max(stored_[static_cast<size_t>(i)], maxS - 45.0f); }
        sa /= kWindow; sb /= kWindow;
        double ab = 0, aa = 0, bb = 0;
        for (int i = 0; i < kWindow; ++i) {
            const double x = std::max(live_[static_cast<size_t>(i)], maxL - 45.0f) - sa, y = std::max(stored_[static_cast<size_t>(i)], maxS - 45.0f) - sb;
            ab += x * y; aa += x * x; bb += y * y;
        }
        if (aa < 300.0 || bb < 300.0) return;          // too steady to tell
        corr = ab / std::sqrt(aa * bb);
    }
    if (corr < 0.35) {
        if (++badWindows_ >= 2) stale_ = true;
    } else {
        badWindows_ = 0;
        if (corr > 0.6) stale_ = false;
    }
}

} // namespace wers
