#include "VoiceMask.h"

namespace refren {

namespace {
constexpr double kLookAheadSeconds = 0.020, kFullSwingSeconds = 0.003, kRingSeconds = 0.03;
constexpr float kStopShare = 0.2f;
}

void VoiceMask::prepare(double fs) {
    classifier_.prepare(fs);
    lookAhead_ = static_cast<int>(std::lround(kLookAheadSeconds * fs));
    hops_ = std::clamp(static_cast<int>(std::ceil(kLookAheadSeconds / classifier_.hopSeconds())), 1, kMaxHops);
    maxStep_ = static_cast<float>(1.0 / (kFullSwingSeconds * fs));
    int size = 1;
    while (size < static_cast<int>(kRingSeconds * fs) + 2) size *= 2;
    ring_.assign(static_cast<size_t>(size), 1.0f);
    ringMask_ = size - 1;
    reset();
}

void VoiceMask::reset() noexcept {
    classifier_.reset();
    shares_.fill(0.0f);
    std::fill(ring_.begin(), ring_.end(), 1.0f);
    hopPos_ = 0;
    count_ = 0;
    lowest_ = 0.0f;
    now_ = 1.0f;
}

float VoiceMask::push(float mono) noexcept {
    if (classifier_.push(mono)) {
        shares_[static_cast<size_t>(hopPos_)] = wers::voicedShare(classifier_.current(), kStopShare);
        hopPos_ = (hopPos_ + 1) % hops_;
        float lowest = 1.0f;
        for (int i = 0; i < hops_; ++i) lowest = std::min(lowest, shares_[static_cast<size_t>(i)]);
        lowest_ = lowest;
    }
    const float wanted = (1.0f - amount_) + amount_ * lowest_;
    now_ += std::clamp(wanted - now_, -maxStep_, maxStep_);
    ring_[static_cast<size_t>(count_ & ringMask_)] = now_;
    ++count_;
    return now_;
}

} // namespace refren
