#pragma once
// Tells how much of the copies of the voice may sound right now. It closes before a sibilant reaches the copies
// and opens again on the vowel after it, so the sharp sounds stay in the middle, in the dry voice.
#include "PhonemeClassifier.h"

namespace refren {

class VoiceMask {
public:
    void prepare(double fs);
    void reset() noexcept;
    // 0 lets everything through, 1 is the whole mask.
    void setAmount(float amount) noexcept { amount_ = amount; }
    // Once a sample. The returned value goes on what the input was the look-ahead ago.
    float push(float mono) noexcept;
    // The value of the mask some samples ago, for a stream with a longer delay.
    float delayed(int samplesAgo) const noexcept { return ring_[static_cast<size_t>((count_ - 1 - samplesAgo) & ringMask_)]; }
    float now() const noexcept { return now_; }
    int lookAheadSamples() const noexcept { return lookAhead_; }
private:
    static constexpr int kMaxHops = 32;
    wers::PhonemeClassifier classifier_;
    std::array<float, kMaxHops> shares_{};
    int hops_ = 10, hopPos_ = 0, lookAhead_ = 960;
    std::vector<float> ring_;
    int64_t ringMask_ = 0, count_ = 0;
    float amount_ = 1.0f, lowest_ = 0.0f, now_ = 1.0f, maxStep_ = 0.007f;
};

} // namespace refren
