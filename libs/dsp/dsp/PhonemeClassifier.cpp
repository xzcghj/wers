#include "PhonemeClassifier.h"

namespace wers {

void PhonemeClassifier::prepare(double fs) {
    fs_ = fs;
    hop_ = std::max(8, static_cast<int>(std::lround(fs * 0.002)));
    low_.set(900.0, 0.7071, fs);
    split_.set(2300.0, 0.7071, fs);
    static constexpr std::array<float, kMaxSub> centres{ 3300.0f, 4700.0f, 6600.0f, 9200.0f, 12500.0f };
    numSub_ = 0;
    for (int i = 0; i < kMaxSub; ++i) {
        if (centres[static_cast<size_t>(i)] < 0.47 * fs) {
            sub_[static_cast<size_t>(numSub_)].set(centres[static_cast<size_t>(i)], 2.0, fs);
            subFreq_[static_cast<size_t>(numSub_)] = centres[static_cast<size_t>(i)];
            ++numSub_;
        }
    }
    const double hopRate = fs / hop_;
    smCoef_ = onePoleCoeff(0.004, hopRate);
    refAtt_ = static_cast<float>(onePoleCoeff(0.03, hopRate));
    refRel_ = static_cast<float>(3.0 / hopRate); // dB per hop, 3 dB per second
    memCoef_ = static_cast<float>(onePoleCoeff(0.002, hopRate));
    breathUp_ = static_cast<float>(onePoleCoeff(0.03, hopRate));
    breathDown_ = static_cast<float>(onePoleCoeff(0.006, hopRate));
    stopHops_ = std::max(1, static_cast<int>(std::lround(0.012 * hopRate)));
    reset();
}

void PhonemeClassifier::reset() noexcept {
    low_.reset(); split_.reset();
    for (auto& s : sub_) s.reset();
    eLow_ = eMid_ = eHigh_ = eAll_ = 0;
    eSub_.fill(0); smSub_.fill(0);
    smLow_ = smMid_ = smHigh_ = smAll_ = 0;
    hist_.fill(-120.0f); histPos_ = 0;
    count_ = 0; stopTimerHops_ = 0; breathAcc_ = 0; refDb_ = -40.0f;
    m_ = Memberships{};
}

void PhonemeClassifier::finishHop() noexcept {
    const double n = count_;
    count_ = 0;
    smLow_ += (eLow_ / n - smLow_) * smCoef_;
    smMid_ += (eMid_ / n - smMid_) * smCoef_;
    smHigh_ += (eHigh_ / n - smHigh_) * smCoef_;
    smAll_ += (eAll_ / n - smAll_) * smCoef_;
    double subSum = 0, subWeighted = 0;
    for (int i = 0; i < numSub_; ++i) {
        auto& s = smSub_[static_cast<size_t>(i)];
        s += (eSub_[static_cast<size_t>(i)] / n - s) * smCoef_;
        subSum += s;
        subWeighted += s * subFreq_[static_cast<size_t>(i)];
        eSub_[static_cast<size_t>(i)] = 0;
    }
    eLow_ = eMid_ = eHigh_ = eAll_ = 0;

    const float low = powerToDb(smLow_), mid = powerToDb(smMid_), high = powerToDb(smHigh_), level = powerToDb(smAll_);

    // Reference follows loud voiced sound: fast up, slow down, never below a floor.
    if (level > refDb_ && low > high) refDb_ += (level - refDb_) * refAtt_;
    else refDb_ = std::max(-70.0f, refDb_ - refRel_);
    const float rel = level - refDb_;

    // Burst detector: a rapid rise after a quiet gap.
    const auto past = [this](int hops) { return hist_[static_cast<size_t>((histPos_ + kHistory - hops) % kHistory)]; };
    const float rise = level - past(3);
    float pre = 0.0f;
    for (int h = 5; h <= 15; ++h) pre = std::min(pre, past(h) - refDb_);
    hist_[static_cast<size_t>(histPos_)] = level;
    histPos_ = (histPos_ + 1) % kHistory;
    if (rise > 10.0f && pre < -22.0f && rel > -30.0f) stopTimerHops_ = stopHops_;
    const float stopRaw = stopTimerHops_ > 0 ? 1.0f : 0.0f;
    if (stopTimerHops_ > 0) --stopTimerHops_;

    const float silenceRaw = std::max(sigmoid(-62.0f - level, 0.0f, 1.5f), sigmoid(-48.0f - rel, 0.0f, 1.5f));
    const float lowOverHigh = low - high, highOverMid = high - mid;
    const float notSilent = 1.0f - silenceRaw;

    const float vowelRaw = sigmoid(lowOverHigh, 7.0f, 3.0f) * sigmoid(rel, -34.0f, 3.0f) * notSilent;
    const float sibRaw = sigmoid(highOverMid, -1.0f, 2.5f) * sigmoid(-lowOverHigh, 3.0f, 3.5f) * sigmoid(rel, -38.0f, 3.0f) * notSilent;
    const float breathTarget = sigmoid(-22.0f - rel, 0.0f, 3.0f) * sigmoid(8.0f - lowOverHigh, 0.0f, 3.0f) * (1.0f - 0.8f * sibRaw) * notSilent;
    breathAcc_ += (breathTarget - breathAcc_) * (breathTarget > breathAcc_ ? breathUp_ : breathDown_);

    const float keep = 1.0f - stopRaw;
    m_.vowel += (vowelRaw * keep * (1.0f - breathAcc_) - m_.vowel) * memCoef_;
    m_.sib += (sibRaw * keep * (1.0f - 0.5f * breathAcc_) - m_.sib) * memCoef_;
    m_.stop += (stopRaw - m_.stop) * memCoef_;
    m_.breath += (breathAcc_ * keep - m_.breath) * memCoef_;
    m_.silence += (silenceRaw - m_.silence) * memCoef_;
    m_.levelDb = level;
    m_.refDb = refDb_;
    m_.highDb = high;
    if (subSum > 1e-14 && sibRaw > 0.3f) {
        const float c = static_cast<float>(subWeighted / subSum);
        m_.sibCentroidHz += (c - m_.sibCentroidHz) * 0.25f;
    }
}

} // namespace wers
