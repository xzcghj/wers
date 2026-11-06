#include "VoiceAnalysis.h"

namespace refren {

namespace {

constexpr double kTargetRate = 12000.0, kLowestHz = 75.0, kHighestHz = 1000.0;
constexpr double kHopSeconds = 0.0025, kHistorySeconds = 0.06, kReadingSeconds = 0.1;
constexpr double kMarkFilterHz = 1200.0, kLevelSeconds = 0.005;
constexpr double kAntiAliasShare = 0.42; // of the new Nyquist frequency
constexpr double kQ1 = 0.54119610014619698, kQ2 = 1.3065629648763766; // fourth-order Butterworth
constexpr float kDipThreshold = 0.15f, kVoicedConfidence = 0.5f, kInputLimit = 64.0f;
constexpr float kCreakMargin = 0.25f;
constexpr double kQuietPower = 1.0e-7; // -70 dBFS
constexpr double kSearchNear = 0.7, kSearchFar = 1.3, kDistancePenalty = 0.25;

float squaredDifference(const float* a, const float* b, int n) noexcept {
    float sum = 0.0f;
    int i = 0;
#if WERS_HAS_SSE
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) {
        const __m128 d = _mm_sub_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i));
        acc = _mm_add_ps(acc, _mm_mul_ps(d, d));
    }
    float parts[4];
    _mm_storeu_ps(parts, acc);
    sum = parts[0] + parts[1] + parts[2] + parts[3];
#endif
    for (; i < n; ++i) {
        const float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

int nextPowerOfTwo(int n) noexcept {
    int size = 1;
    while (size < n) size *= 2;
    return size;
}

} // namespace

PitchReading settledReading(const std::array<PitchReading, 5>& around) noexcept {
    std::array<float, 5> periods{};
    float confidence = 0.0f;
    int voiced = 0;
    for (const auto& r : around) {
        if (r.voiced) {
            periods[static_cast<size_t>(voiced++)] = r.period;
            confidence = std::max(confidence, r.confidence);
        }
    }
    PitchReading settled;
    if (voiced >= 3) {
        for (int i = 1; i < voiced; ++i) {
            const float value = periods[static_cast<size_t>(i)];
            int k = i;
            for (; k > 0 && periods[static_cast<size_t>(k - 1)] > value; --k) periods[static_cast<size_t>(k)] = periods[static_cast<size_t>(k - 1)];
            periods[static_cast<size_t>(k)] = value;
        }
        settled.period = periods[static_cast<size_t>(voiced / 2)];
        settled.confidence = confidence;
        settled.voiced = true;
    }
    return settled;
}

void VoiceAnalysis::prepare(double fs) {
    fs_ = fs;
    decimation_ = std::max(1, static_cast<int>(std::lround(fs / kTargetRate)));
    const double low = fs / decimation_;
    hopDecimated_ = std::max(1, static_cast<int>(std::lround(kHopSeconds * low)));
    hopSamples_ = hopDecimated_ * decimation_;
    tauMax_ = static_cast<int>(std::ceil(low / kLowestHz));
    tauMin_ = std::max(2, static_cast<int>(std::floor(low / kHighestHz)));
    window_ = tauMax_;
    decimatedSize_ = window_ + tauMax_ + 2;
    decimated_.assign(static_cast<size_t>(decimatedSize_) * 2, 0.0f);
    difference_.assign(static_cast<size_t>(tauMax_) + 2, 0.0f);
    // The centre of the window, and two more readings for the two neighbours a settled reading waits for.
    readingLag_ = (window_ + tauMax_) * decimation_ / 2 + 2 * hopSamples_;

    const int ringSize = nextPowerOfTwo(static_cast<int>(std::ceil(fs * kHistorySeconds)) + 8);
    ring_.assign(static_cast<size_t>(ringSize), 0.0f);
    lowRing_.assign(static_cast<size_t>(ringSize), 0.0f);
    ringMask_ = ringSize - 1;
    readings_.assign(static_cast<size_t>(std::ceil(kReadingSeconds * fs / hopSamples_)) + 1, PitchReading{});

    using Type = wers::Biquad::Type;
    antiAlias_[0].set(Type::LowPass, kAntiAliasShare * 0.5 * low, kQ1, 0.0, fs);
    antiAlias_[1].set(Type::LowPass, kAntiAliasShare * 0.5 * low, kQ2, 0.0, fs);
    markFilter_.set(Type::LowPass, kMarkFilterHz, 0.7071, 0.0, fs);

    powerCoef_ = wers::onePoleCoeff(kLevelSeconds, fs);
    levelShift_ = static_cast<int>(std::lround(kLevelSeconds * fs));
    levelRing_.assign(static_cast<size_t>(std::ceil(fs * kHistorySeconds / decimation_)) + 4, 0.0f);
    reset();
}

void VoiceAnalysis::reset() noexcept {
    now_ = nextMarkAt_ = lastMark_ = newestReadingTime_ = levelCount_ = 0;
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    std::fill(lowRing_.begin(), lowRing_.end(), 0.0f);
    std::fill(decimated_.begin(), decimated_.end(), 0.0f);
    std::fill(readings_.begin(), readings_.end(), PitchReading{});
    raw_.fill(PitchReading{});
    std::fill(levelRing_.begin(), levelRing_.end(), 0.0f);
    decimatedPos_ = decimateCount_ = hopCount_ = readingPos_ = readingCount_ = markPos_ = markCount_ = 0;
    chain_ = false;
    power_ = 0.0;
    for (auto& f : antiAlias_) f.reset();
    markFilter_.reset();
}

void VoiceAnalysis::push(float mono) noexcept {
    const float x = std::isfinite(mono) ? std::clamp(mono, -kInputLimit, kInputLimit) : 0.0f;
    const auto place = static_cast<size_t>(now_ & ringMask_);
    ring_[place] = x;
    lowRing_[place] = static_cast<float>(markFilter_.tick(static_cast<double>(x)));
    power_ += (static_cast<double>(x) * x - power_) * powerCoef_;
    ++now_;
    const double smooth = antiAlias_[1].tick(antiAlias_[0].tick(static_cast<double>(x)));
    if (++decimateCount_ >= decimation_) {
        decimateCount_ = 0;
        const auto low = static_cast<float>(smooth);
        decimated_[static_cast<size_t>(decimatedPos_)] = low;
        decimated_[static_cast<size_t>(decimatedPos_ + decimatedSize_)] = low;
        if (++decimatedPos_ >= decimatedSize_) decimatedPos_ = 0;
        levelRing_[static_cast<size_t>(levelCount_ % static_cast<int64_t>(levelRing_.size()))] = static_cast<float>(std::sqrt(power_));
        ++levelCount_;
        if (++hopCount_ >= hopDecimated_) {
            hopCount_ = 0;
            analyse();
        }
    }
    if (now_ >= nextMarkAt_) placeMarks();
}

void VoiceAnalysis::analyse() noexcept {
    // decimatedPos_ is the oldest sample, so from there on the history is in order.
    const float* oldest = decimated_.data() + decimatedPos_ + 2;
    const int newestFirstSample = window_ + tauMax_ - 1;
    const float* last = oldest + newestFirstSample - window_ + 1; // the last window_ samples

    PitchReading reading;
    double energy = 0.0;
    for (int i = 0; i < window_; ++i) energy += static_cast<double>(last[i]) * last[i];
    if (energy / window_ >= kQuietPower) {
        // Cumulative mean normalised difference: what stays low is what repeats.
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax_; ++tau) {
            const float d = squaredDifference(last, last - tau, window_);
            running += d;
            difference_[static_cast<size_t>(tau)] = running > 0.0f ? d * static_cast<float>(tau) / running : 1.0f;
        }
        int best = 0;
        for (int tau = tauMin_; tau < tauMax_; ++tau) {
            if (difference_[static_cast<size_t>(tau)] < kDipThreshold) {
                while (tau + 1 < tauMax_ && difference_[static_cast<size_t>(tau + 1)] < difference_[static_cast<size_t>(tau)]) ++tau;
                best = tau;
                break;
            }
        }
        if (best == 0) {
            best = tauMin_;
            for (int tau = tauMin_; tau < tauMax_; ++tau)
                if (difference_[static_cast<size_t>(tau)] < difference_[static_cast<size_t>(best)]) best = tau;
        }
        // A creaky voice repeats exactly only every two periods, but what is heard is the pitch of its pulses.
        while (best / 2 >= tauMin_) {
            int half = best / 2;
            if (difference_[static_cast<size_t>(half + 1)] < difference_[static_cast<size_t>(half)]) ++half;
            else if (half > tauMin_ && difference_[static_cast<size_t>(half - 1)] < difference_[static_cast<size_t>(half)]) --half;
            const bool isDip = difference_[static_cast<size_t>(half)] <= difference_[static_cast<size_t>(half - 1)] && difference_[static_cast<size_t>(half)] <= difference_[static_cast<size_t>(half + 1)];
            if (!isDip || difference_[static_cast<size_t>(half)] >= difference_[static_cast<size_t>(best)] + kCreakMargin) break;
            best = half;
        }
        const float before = difference_[static_cast<size_t>(best - 1)], at = difference_[static_cast<size_t>(best)], after = difference_[static_cast<size_t>(best + 1)];
        const float curve = before - 2.0f * at + after;
        const float shift = curve > 1.0e-9f ? std::clamp(0.5f * (before - after) / curve, -1.0f, 1.0f) : 0.0f;
        reading.period = (static_cast<float>(best) + shift) * static_cast<float>(decimation_);
        reading.confidence = std::clamp(1.0f - at, 0.0f, 1.0f);
        reading.voiced = reading.confidence >= kVoicedConfidence;
    }
    std::rotate(raw_.begin(), raw_.begin() + 1, raw_.end());
    raw_.back() = reading;
    readings_[static_cast<size_t>(readingPos_)] = settledReading(raw_);
    readingPos_ = (readingPos_ + 1) % static_cast<int>(readings_.size());
    readingCount_ = std::min(readingCount_ + 1, static_cast<int>(readings_.size()));
    newestReadingTime_ = now_ - readingLag_;
}

PitchReading VoiceAnalysis::pitchAt(int64_t time) const noexcept {
    if (readingCount_ == 0) return {};
    const int64_t distance = newestReadingTime_ - time;
    const int64_t behind = distance <= 0 ? 0 : std::min<int64_t>((distance + hopSamples_ / 2) / hopSamples_, readingCount_ - 1);
    const int size = static_cast<int>(readings_.size());
    return readings_[static_cast<size_t>((readingPos_ - 1 - static_cast<int>(behind) + 2 * size) % size)];
}

void VoiceAnalysis::addMark(int64_t position, float period) noexcept {
    marks_[static_cast<size_t>(markPos_)] = { position, period };
    markPos_ = (markPos_ + 1) % kMarks;
    markCount_ = std::min(markCount_ + 1, kMarks);
    lastMark_ = position;
}

void VoiceAnalysis::placeMarks() noexcept {
    const PitchReading newest = pitchAt(newestReadingTime_);
    if (!newest.voiced) {
        chain_ = false;
        nextMarkAt_ = now_ + hopSamples_;
        return;
    }
    const float period = newest.period;
    if (!chain_) {
        // Into a voice: the highest point of the last period.
        const auto length = static_cast<int64_t>(period);
        int64_t best = now_ - 1;
        for (int64_t t = now_ - length; t < now_; ++t)
            if (lowAt(t) > lowAt(best)) best = t;
        addMark(best, period);
        chain_ = true;
        nextMarkAt_ = best + static_cast<int64_t>(kSearchFar * period) + 1;
        return;
    }
    const int64_t from = lastMark_ + static_cast<int64_t>(kSearchNear * period);
    const int64_t to = lastMark_ + static_cast<int64_t>(kSearchFar * period);
    if (now_ <= to) {
        nextMarkAt_ = to + 1;
        return;
    }
    // The highest point near one period on, so another high point of the same period does not win.
    float peak = 0.0f;
    for (int64_t t = from; t <= to; ++t) peak = std::max(peak, std::abs(lowAt(t)));
    const double centre = static_cast<double>(lastMark_) + static_cast<double>(period);
    const double slope = kDistancePenalty * static_cast<double>(peak) / ((kSearchFar - 1.0) * static_cast<double>(period));
    int64_t best = from;
    double bestScore = -1.0e30;
    for (int64_t t = from; t <= to; ++t) {
        const double score = static_cast<double>(lowAt(t)) - slope * std::abs(static_cast<double>(t) - centre);
        if (score > bestScore) {
            bestScore = score;
            best = t;
        }
    }
    addMark(best, period);
    nextMarkAt_ = best + static_cast<int64_t>(kSearchFar * period) + 1;
}

bool VoiceAnalysis::markNear(int64_t time, int64_t latest, Mark& out) const noexcept {
    bool found = false;
    int64_t bestDistance = 0;
    for (int k = 0; k < markCount_; ++k) {
        const Mark& mark = marks_[static_cast<size_t>((markPos_ - 1 - k + 2 * kMarks) % kMarks)];
        if (mark.position > latest) continue;
        const int64_t distance = std::abs(mark.position - time);
        if (found && distance >= bestDistance) break; // older marks only get further away
        if (static_cast<double>(distance) <= 1.5 * static_cast<double>(mark.period)) {
            found = true;
            bestDistance = distance;
            out = mark;
        }
    }
    return found;
}

float VoiceAnalysis::sample(double time) const noexcept {
    const double whole = std::floor(time);
    const auto i = static_cast<int64_t>(whole);
    const auto t = static_cast<float>(time - whole);
    const float a = ring_[static_cast<size_t>((i - 1) & ringMask_)], b = ring_[static_cast<size_t>(i & ringMask_)];
    const float c = ring_[static_cast<size_t>((i + 1) & ringMask_)], d = ring_[static_cast<size_t>((i + 2) & ringMask_)];
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}

float VoiceAnalysis::levelAt(int64_t time) const noexcept {
    if (levelCount_ == 0) return 0.0f;
    // The level follows the sound with a small delay, which is taken back here.
    const int64_t index = std::clamp<int64_t>((time + levelShift_) / decimation_, levelCount_ - static_cast<int64_t>(levelRing_.size()) + 1, levelCount_ - 1);
    return levelRing_[static_cast<size_t>(std::max<int64_t>(index, 0) % static_cast<int64_t>(levelRing_.size()))];
}

} // namespace refren
