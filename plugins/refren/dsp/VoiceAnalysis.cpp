#include "VoiceAnalysis.h"

namespace refren {

namespace {

constexpr double kTargetRate = 12000.0, kLowestHz = 75.0, kHighestHz = 1000.0;
constexpr double kHopSeconds = 0.0025, kHistorySeconds = 0.06, kReadingSeconds = 0.1;
constexpr double kMarkFilterHz = 1200.0, kLevelSeconds = 0.005;
constexpr double kAntiAliasShare = 0.42; // of the new Nyquist frequency
constexpr double kQ1 = 0.54119610014619698, kQ2 = 1.3065629648763766; // fourth-order Butterworth
constexpr float kDipThreshold = 0.15f, kVoicedConfidence = 0.5f, kInputLimit = 64.0f;
constexpr float kDipMargin = 0.25f; // how far above the deepest dip another one can still be the period
// How much worse than the best dip the highs of a dip may repeat for it to be the period. Without any memory of the
// voice a dip must do well. What the voice has just been doing may do a lot worse, so a creaky stretch stays where it
// is, and anything else has to do as well as the best, so a resonance does not move the reading. With highs this
// poor, they say nothing.
constexpr float kHighsWithoutMemory = 0.25f, kHighsRemembered = 0.8f, kHighsOther = 0.05f, kHighsSayNothing = 0.6f;
constexpr float kOtherDipMargin = 0.05f; // a dip that is not the remembered one is no less clean than it
constexpr float kUsualStepOctaves = 0.0025f, kUsualRangeOctaves = 0.5f; // one octave takes a second of voice
constexpr double kRefineSeconds = 0.0133;
constexpr int kRefineLongest = 1024; // keeps the work at the highest sample rates within reason
constexpr double kQuietPower = 1.0e-7; // -70 dBFS
constexpr double kSearchFar = 1.3, kDistancePenalty = 0.25;
constexpr float kEarlierPeriodLevel = 0.25f; // walking back into a voice stops where it is this much quieter

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
    highs_.assign(static_cast<size_t>(window_ + tauMax_), 0.0f);
    // The centre of the window, and two more readings for the two neighbours a settled reading waits for.
    readingLagOfWindow_ = (window_ + tauMax_) * decimation_ / 2;
    readingLag_ = readingLagOfWindow_ + 2 * hopSamples_;
    refineWindow_ = std::min(static_cast<int>(std::lround(fs * kRefineSeconds)), kRefineLongest);
    refine_.assign(static_cast<size_t>(refineWindow_) + static_cast<size_t>(tauMax_ + 2) * static_cast<size_t>(decimation_) + 8, 0.0f);

    const int ringSize = nextPowerOfTwo(static_cast<int>(std::ceil(fs * kHistorySeconds)) + 8);
    ring_.assign(static_cast<size_t>(ringSize), 0.0f);
    lowRing_.assign(static_cast<size_t>(ringSize), 0.0f);
    smoothRing_.assign(static_cast<size_t>(ringSize), 0.0f);
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
    std::fill(smoothRing_.begin(), smoothRing_.end(), 0.0f);
    std::fill(decimated_.begin(), decimated_.end(), 0.0f);
    std::fill(readings_.begin(), readings_.end(), PitchReading{});
    raw_.fill(PitchReading{});
    std::fill(levelRing_.begin(), levelRing_.end(), 0.0f);
    decimatedPos_ = decimateCount_ = hopCount_ = readingPos_ = readingCount_ = markPos_ = markCount_ = 0;
    chain_ = false;
    power_ = 0.0;
    lastTau_ = voicedHops_ = 0;
    usualOctaves_ = 0.0f;
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
    smoothRing_[place] = static_cast<float>(smooth);
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
    // decimatedPos_ is the oldest sample, so from there on the history is in order. The two arms of the comparison
    // move apart around one moment, so a reading always belongs to that moment, whatever period it finds.
    const float* oldest = decimated_.data() + decimatedPos_ + 2;
    const float* middle = oldest + tauMax_ / 2;

    PitchReading reading;
    double energy = 0.0;
    for (int i = 0; i < window_; ++i) energy += static_cast<double>(middle[i]) * middle[i];
    if (energy / window_ >= kQuietPower) {
        // Cumulative mean normalised difference: what stays low is what repeats.
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax_; ++tau) {
            const int back = tau / 2;
            const float d = squaredDifference(middle - back, middle - back + tau, window_);
            running += d;
            difference_[static_cast<size_t>(tau)] = running > 0.0f ? d * static_cast<float>(tau) / running : 1.0f;
        }
        int deepest = tauMin_;
        for (int tau = tauMin_; tau < tauMax_; ++tau)
            if (difference_[static_cast<size_t>(tau)] < difference_[static_cast<size_t>(deepest)]) deepest = tau;
        int best = deepest;
        for (int tau = tauMin_; tau < tauMax_; ++tau) {
            if (difference_[static_cast<size_t>(tau)] < kDipThreshold) {
                while (tau + 1 < tauMax_ && difference_[static_cast<size_t>(tau + 1)] < difference_[static_cast<size_t>(tau)]) ++tau;
                best = tau;
                break;
            }
        }
        best = periodAmongDips(oldest, best, deepest);
        const float before = difference_[static_cast<size_t>(best - 1)], at = difference_[static_cast<size_t>(best)], after = difference_[static_cast<size_t>(best + 1)];
        const float curve = before - 2.0f * at + after;
        const float shift = curve > 1.0e-9f ? std::clamp(0.5f * (before - after) / curve, -1.0f, 1.0f) : 0.0f;
        // Whichever dip is the period, the deepest one says how well the sound repeats at all.
        reading.confidence = std::clamp(1.0f - difference_[static_cast<size_t>(deepest)], 0.0f, 1.0f);
        reading.voiced = reading.confidence >= kVoicedConfidence;
        reading.period = reading.voiced ? refinedPeriod((static_cast<float>(best) + shift) * static_cast<float>(decimation_)) : (static_cast<float>(best) + shift) * static_cast<float>(decimation_);
    }
    std::rotate(raw_.begin(), raw_.begin() + 1, raw_.end());
    raw_.back() = reading;
    const PitchReading settled = settledReading(raw_);
    readings_[static_cast<size_t>(readingPos_)] = settled;
    // A voice is worth remembering once it fills the whole comparison, because at its start the curve sees only a part.
    voicedHops_ = settled.voiced ? voicedHops_ + 1 : 0;
    lastTau_ = 0;
    if (voicedHops_ * hopDecimated_ > window_ + tauMax_) {
        lastTau_ = static_cast<int>(std::lround(settled.period / static_cast<float>(decimation_)));
        const float octaves = std::log2(static_cast<float>(lastTau_));
        usualOctaves_ = usualOctaves_ <= 0.0f ? octaves : usualOctaves_ + std::clamp(octaves - usualOctaves_, -kUsualStepOctaves, kUsualStepOctaves);
    }
    readingPos_ = (readingPos_ + 1) % static_cast<int>(readings_.size());
    readingCount_ = std::min(readingCount_ + 1, static_cast<int>(readings_.size()));
    newestReadingTime_ = now_ - readingLag_;
}

int VoiceAnalysis::periodAmongDips(const float* oldest, int classicChoice, int deepest) noexcept {
    // A resonance on a low harmonic makes the sound repeat at a fraction of its period, and a creaky voice makes it
    // repeat exactly only every second period. Both look alike in this curve. The highs tell them apart: what the
    // glottis excites up there rings out within one period, so it comes back with the true period and only then,
    // while the pulses of a creaky voice bring it back each time.
    constexpr int kMostDips = 12;
    const float limit = difference_[static_cast<size_t>(deepest)] + kDipMargin;
    std::array<int, kMostDips> dips{};
    int count = 0;
    bool hasDeepest = false;
    for (int tau = tauMin_; tau < tauMax_; ++tau) {
        const float at = difference_[static_cast<size_t>(tau)];
        if (at >= limit || at > difference_[static_cast<size_t>(tau - 1)] || at > difference_[static_cast<size_t>(tau + 1)]) continue;
        if (count == kMostDips) {
            if (hasDeepest) break;
            if (tau != deepest) continue;
            count = kMostDips - 1;
        }
        dips[static_cast<size_t>(count++)] = tau;
        hasDeepest = hasDeepest || tau == deepest;
    }
    if (count < 2) return classicChoice;

    const int last = window_ + tauMax_ - 1;
    for (int i = 0; i < last; ++i) highs_[static_cast<size_t>(i)] = oldest[i - 1] - 2.0f * oldest[i] + oldest[i + 1];
    const float* middle = highs_.data() + tauMax_ / 2;
    std::array<float, kMostDips> highs{};
    float bestHighs = 1.0e9f;
    for (int k = 0; k < count; ++k) {
        const int tau = dips[static_cast<size_t>(k)];
        const float* a = middle - tau / 2;
        const float* b = a + tau;
        float power = 0.0f;
        for (int i = 0; i < window_; ++i) power += a[i] * a[i] + b[i] * b[i];
        // 0 when they are the same, 1 when they have nothing to do with each other.
        highs[static_cast<size_t>(k)] = power > 0.0f ? squaredDifference(a, b, window_) / power : 1.0f;
        bestHighs = std::min(bestHighs, highs[static_cast<size_t>(k)]);
    }

    const auto worse = [&](int k) { return highs[static_cast<size_t>(k)] - bestHighs; };
    const auto nearest = [&](float wanted) {
        int found = 0;
        for (int k = 1; k < count; ++k)
            if (std::abs(std::log2(static_cast<float>(dips[static_cast<size_t>(k)]) / wanted)) < std::abs(std::log2(static_cast<float>(dips[static_cast<size_t>(found)]) / wanted))) found = k;
        return found;
    };
    // What the voice was just doing, or else where it usually is.
    int kept = -1;
    if (lastTau_ > 0) {
        const int k = nearest(static_cast<float>(lastTau_));
        if (std::abs(dips[static_cast<size_t>(k)] - lastTau_) * 5 <= lastTau_) kept = k;
    }
    if (kept < 0 && usualOctaves_ > 0.0f) {
        const int k = nearest(std::pow(2.0f, usualOctaves_));
        if (std::abs(std::log2(static_cast<float>(dips[static_cast<size_t>(k)])) - usualOctaves_) <= kUsualRangeOctaves) kept = k;
    }
    if (bestHighs >= kHighsSayNothing) return kept >= 0 ? dips[static_cast<size_t>(kept)] : classicChoice;

    if (kept < 0) {
        // Nothing to go by but the sound. From the classic choice on, the first whose highs come back too, and then
        // its half, for a creaky voice.
        int chosen = 0;
        while (chosen < count - 1 && dips[static_cast<size_t>(chosen)] < classicChoice) ++chosen;
        while (chosen < count - 1 && worse(chosen) > kHighsWithoutMemory) ++chosen;
        for (int k = chosen - 1; k >= 0; --k) {
            if (std::abs(2 * dips[static_cast<size_t>(k)] - dips[static_cast<size_t>(chosen)]) * 20 <= dips[static_cast<size_t>(chosen)] && worse(k) <= kHighsWithoutMemory) chosen = k;
        }
        return dips[static_cast<size_t>(chosen)];
    }
    const int keptTau = dips[static_cast<size_t>(kept)];
    // A voice leaps up by an octave and not by any other step, and only when its highs and all the rest come back there.
    for (int k = 0; k < kept; ++k) {
        const int tau = dips[static_cast<size_t>(k)];
        if (std::abs(2 * tau - keptTau) * 5 <= keptTau && worse(k) <= kHighsOther && difference_[static_cast<size_t>(tau)] <= difference_[static_cast<size_t>(keptTau)] + kOtherDipMargin) return tau;
    }
    if (worse(kept) <= kHighsRemembered) return keptTau;
    for (int k = kept + 1; k < count; ++k)
        if (worse(k) <= kHighsOther) return dips[static_cast<size_t>(k)];
    return classicChoice;
}

float VoiceAnalysis::refinedPeriod(float period) noexcept {
    // The same comparison at the host rate, close around the period found, for what a low rate cannot tell apart.
    const int centreLag = static_cast<int>(std::lround(period));
    const int reach = decimation_ + 1;
    const int length = refineWindow_;
    const int longestLag = centreLag + reach + 1;
    const int span = length + longestLag;
    if (span > static_cast<int>(refine_.size())) return period;
    // Around the moment the reading belongs to.
    const int64_t start = now_ - readingLagOfWindow_ - span / 2;
    for (int i = 0; i < span; ++i) refine_[static_cast<size_t>(i)] = smoothRing_[static_cast<size_t>((start + i) & ringMask_)];
    const float* centre = refine_.data() + longestLag / 2;
    std::array<float, 3> around{};
    int bestLag = 0;
    float bestValue = 1.0e30f, before = 0.0f;
    for (int lag = centreLag - reach; lag <= centreLag + reach; ++lag) {
        const int back = lag / 2;
        const float value = squaredDifference(centre - back, centre - back + lag, length);
        if (value < bestValue) {
            bestValue = value;
            bestLag = lag;
            around[0] = before;
            around[1] = value;
            around[2] = value; // until the next lag is known
        } else if (lag == bestLag + 1) {
            around[2] = value;
        }
        before = value;
    }
    if (bestLag <= centreLag - reach || bestLag >= centreLag + reach) return period;
    const float curve = around[0] - 2.0f * around[1] + around[2];
    return static_cast<float>(bestLag) + (curve > 1.0e-12f ? 0.5f * (around[0] - around[2]) / curve : 0.0f);
}

PitchReading VoiceAnalysis::pitchAt(int64_t time) const noexcept {
    if (readingCount_ == 0) return {};
    const int64_t distance = newestReadingTime_ - time;
    const int64_t behind = distance <= 0 ? 0 : std::min<int64_t>((distance + hopSamples_ / 2) / hopSamples_, readingCount_ - 1);
    const int size = static_cast<int>(readings_.size());
    return readings_[static_cast<size_t>((readingPos_ - 1 - static_cast<int>(behind) + 2 * size) % size)];
}

void VoiceAnalysis::addMark(int64_t position, float period) noexcept {
    // The peak lies between samples. Without this a period that is no whole number of samples would make every
    // piece of a stream start up to half a sample off, which is a lot at a high pitch.
    float fraction = 0.0f;
    if (position + 1 < now_) {
        const float before = lowAt(position - 1), at = lowAt(position), after = lowAt(position + 1);
        const float curve = before - 2.0f * at + after;
        if (curve < 0.0f) fraction = std::clamp(0.5f * (before - after) / curve, -0.5f, 0.5f);
    }
    marks_[static_cast<size_t>(markPos_)] = { position, fraction, period };
    markPos_ = (markPos_ + 1) % kMarks;
    markCount_ = std::min(markCount_ + 1, kMarks);
    lastMark_ = position;
}

int64_t VoiceAnalysis::peakNear(int64_t place, float period) const noexcept {
    // The highest point near one period away, so another high point of the same period does not win.
    const auto from = place - static_cast<int64_t>((kSearchFar - 1.0) * static_cast<double>(period));
    const auto to = place + static_cast<int64_t>((kSearchFar - 1.0) * static_cast<double>(period));
    float peak = 0.0f;
    for (int64_t t = from; t <= to; ++t) peak = std::max(peak, std::abs(lowAt(t)));
    const double slope = kDistancePenalty * static_cast<double>(peak) / ((kSearchFar - 1.0) * static_cast<double>(period));
    int64_t best = from;
    double bestScore = -1.0e30;
    for (int64_t t = from; t <= to; ++t) {
        const double score = static_cast<double>(lowAt(t)) - slope * std::abs(static_cast<double>(t - place));
        if (score > bestScore) {
            bestScore = score;
            best = t;
        }
    }
    return best;
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
        // Into a voice: the highest point of the last period. A reading comes after the sound it is about, so the
        // periods that were already there are found by walking back from that point.
        const auto length = static_cast<int64_t>(period);
        int64_t best = now_ - 1;
        for (int64_t t = now_ - length; t < now_; ++t)
            if (lowAt(t) > lowAt(best)) best = t;
        const float level = lowAt(best);
        const int64_t earliest = std::max(now_ - readingLag_ - length, lastMark_ + length / 2 + 1);
        std::array<int64_t, kMarks> found{};
        int count = 0;
        found[static_cast<size_t>(count++)] = best;
        while (count < kMarks) {
            const int64_t place = found[static_cast<size_t>(count - 1)] - length;
            if (place - length / 2 < earliest) break;
            const int64_t candidate = peakNear(place, period);
            if (lowAt(candidate) < kEarlierPeriodLevel * level) break;
            found[static_cast<size_t>(count++)] = candidate;
        }
        for (int k = count - 1; k >= 0; --k) addMark(found[static_cast<size_t>(k)], period);
        chain_ = true;
        nextMarkAt_ = best + static_cast<int64_t>(kSearchFar * static_cast<double>(period)) + 1;
        return;
    }
    const int64_t to = lastMark_ + static_cast<int64_t>(kSearchFar * static_cast<double>(period));
    if (now_ <= to) {
        nextMarkAt_ = to + 1;
        return;
    }
    const int64_t best = peakNear(lastMark_ + static_cast<int64_t>(std::lround(period)), period);
    addMark(best, period);
    nextMarkAt_ = best + static_cast<int64_t>(kSearchFar * static_cast<double>(period)) + 1;
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
