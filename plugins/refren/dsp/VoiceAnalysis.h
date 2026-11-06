#pragma once
// What Refren knows about the voice: its recent sound, its pitch, where each of its periods stands and how
// loud it is. Everything is looked up by the absolute time of a sample, so whatever plays later can ask
// about the very moment it is playing. All memory is allocated in prepare().
#include "Filters.h"

namespace refren {

struct PitchReading {
    float period = 0.0f;     // in samples at the host rate
    float confidence = 0.0f; // 0 to 1
    bool voiced = false;
};

struct Mark {
    int64_t position = 0;
    float fraction = 0.0f; // the rest of the place, between samples
    float period = 0.0f;
};

// Five neighbouring readings make one: a voice is there if most of them say so, with the middle of their periods.
// A lone wrong reading, an octave off or a gap, then decides nothing.
PitchReading settledReading(const std::array<PitchReading, 5>& around) noexcept;

class VoiceAnalysis {
public:
    void prepare(double fs);
    void reset() noexcept;
    void push(float mono) noexcept;
    // How many samples were pushed. The newest one has the time now() - 1.
    int64_t now() const noexcept { return now_; }
    // The input at any moment of the last 60 ms, between samples too.
    float sample(double time) const noexcept;
    // The reading of the analysis window centred nearest to that moment.
    PitchReading pitchAt(int64_t time) const noexcept;
    // The mark nearest to that moment, not later than latest and at most one and a half periods away.
    bool markNear(int64_t time, int64_t latest, Mark& out) const noexcept;
    // The level of the input around that moment, as a linear value.
    float levelAt(int64_t time) const noexcept;
private:
    static constexpr int kMarks = 64;
    void analyse() noexcept;
    int periodAmongDips(const float* oldest, int classicChoice, int deepest) noexcept;
    float refinedPeriod(float period) noexcept;
    void placeMarks() noexcept;
    int64_t peakNear(int64_t place, float period) const noexcept;
    void addMark(int64_t position, float period) noexcept;
    float lowAt(int64_t time) const noexcept { return lowRing_[static_cast<size_t>(time & ringMask_)]; }

    double fs_ = 48000.0;
    int decimation_ = 4, hopDecimated_ = 30, hopSamples_ = 120, tauMin_ = 12, tauMax_ = 160, window_ = 160, readingLag_ = 640, levelShift_ = 240;
    int64_t ringMask_ = 0, now_ = 0, nextMarkAt_ = 0, lastMark_ = 0, newestReadingTime_ = 0;
    std::vector<float> ring_, lowRing_, smoothRing_, refine_;
    int refineWindow_ = 640, readingLagOfWindow_ = 640;
    std::vector<float> decimated_, difference_, highs_; // decimated_ is written twice, so a window is always contiguous
    int lastTau_ = 0, voicedHops_ = 0; // the period of the last reading, at the low rate, while the voice goes on
    float usualOctaves_ = 0.0f; // where the voice usually is, as the logarithm of that period
    int decimatedSize_ = 0, decimatedPos_ = 0, decimateCount_ = 0, hopCount_ = 0;
    std::array<wers::Biquad, 2> antiAlias_{};
    wers::Biquad markFilter_;
    std::array<PitchReading, 5> raw_{};
    std::vector<PitchReading> readings_; // settled ones
    int readingPos_ = 0, readingCount_ = 0;
    std::array<Mark, kMarks> marks_{};
    int markPos_ = 0, markCount_ = 0;
    bool chain_ = false;
    double power_ = 0.0, powerCoef_ = 0.01;
    std::vector<float> levelRing_;
    int64_t levelCount_ = 0;
};

} // namespace refren
