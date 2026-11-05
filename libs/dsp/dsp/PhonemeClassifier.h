#pragma once
// Soft, causal classification of a voice into sound classes, independent of input level.
// Runs identically at the host rate (realtime) and at 16 kHz (offline analysis).
#include "Filters.h"

namespace wers {

struct Memberships {
    float vowel = 0.0f, sib = 0.0f, stop = 0.0f, breath = 0.0f, silence = 1.0f;
    float sibCentroidHz = 5000.0f;
    float levelDb = -120.0f;     // current level of the voice
    float refDb = -40.0f;        // running reference: the level of loud voiced sound
    float highDb = -120.0f;      // level in the sibilant band
};

class PhonemeClassifier {
public:
    void prepare(double fs);
    void reset() noexcept;
    // Returns true when a new hop was finished and current() changed.
    bool push(float x) noexcept {
        const double lo = low_.tick(x).lp;
        const double hiSplit = split_.tick(x).hp;
        const double mid = x - lo - hiSplit;
        eLow_ += lo * lo; eMid_ += mid * mid; eHigh_ += hiSplit * hiSplit; eAll_ += static_cast<double>(x) * x;
        for (int i = 0; i < numSub_; ++i) {
            const double b = sub_[static_cast<size_t>(i)].band(hiSplit);
            eSub_[static_cast<size_t>(i)] += b * b;
        }
        if (++count_ >= hop_) { finishHop(); return true; }
        return false;
    }
    const Memberships& current() const noexcept { return m_; }
    double hopSeconds() const noexcept { return hop_ / fs_; }
private:
    void finishHop() noexcept;
    static constexpr int kMaxSub = 5;
    static constexpr int kHistory = 24;
    double fs_ = 48000.0;
    int hop_ = 96, count_ = 0, numSub_ = kMaxSub;
    Svf low_, split_;
    std::array<Svf, kMaxSub> sub_{};
    std::array<float, kMaxSub> subFreq_{};
    double eLow_ = 0, eMid_ = 0, eHigh_ = 0, eAll_ = 0;
    std::array<double, kMaxSub> eSub_{};
    double smLow_ = 0, smMid_ = 0, smHigh_ = 0, smAll_ = 0, smCoef_ = 0.5;
    std::array<double, kMaxSub> smSub_{};
    std::array<float, kHistory> hist_{};
    int histPos_ = 0;
    int stopHops_ = 0, stopTimerHops_ = 0;
    float breathAcc_ = 0.0f, refDb_ = -40.0f, refAtt_ = 0.1f, refRel_ = 0.01f, memCoef_ = 0.5f, breathUp_ = 0.1f, breathDown_ = 0.3f;
    Memberships m_;
};

} // namespace wers
