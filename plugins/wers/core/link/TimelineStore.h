#pragma once
// Holds a mono signal at the analysis rate, addressed by project time. One realtime writer, read
// only after the capture stopped. The writer never blocks and never allocates.
#include "analysis/Types.h"
#include "dsp/Filters.h"
#include <atomic>

namespace wers {

class TimelineStore {
public:
    explicit TimelineStore(int maxSeconds = 900) : capacity_(static_cast<size_t>(maxSeconds) * kAnalysisRate) {}

    void prepare(double hostRate) noexcept; // not realtime; call while not capturing
    bool arm();                             // not realtime; allocates on first use, clears old data
    void disarm() noexcept;                 // not realtime; returns once the writer has left
    bool armed() const noexcept { return armed_.load(std::memory_order_relaxed); }

    // Audio thread. timeline is the sample position of the first sample of the block.
    void capture(const float* mono, int numSamples, int64_t timeline) noexcept;
    // Audio thread: same but with two channels summed.
    void captureStereo(const float* l, const float* r, int numSamples, int64_t timeline) noexcept;

    // Reader side, only while disarmed.
    std::vector<float> copy() const;
    double heardSeconds() const noexcept { return static_cast<double>(heardSamples_.load(std::memory_order_relaxed)) / kAnalysisRate; }
    double endSeconds() const noexcept { return static_cast<double>(endIndex_.load(std::memory_order_relaxed)) / kAnalysisRate; }
private:
    inline void writeOne(double sample, double p) noexcept;
    size_t capacity_;
    std::vector<float> data_;
    std::atomic<bool> armed_{ false };
    std::atomic<int> writers_{ 0 };
    std::atomic<int64_t> heardSamples_{ 0 }, endIndex_{ 0 };
    double hostRate_ = 48000.0, ratio_ = 1.0 / 3.0;
    std::array<Biquad, 3> lp_{};
    int64_t expected_ = -1, nextOut_ = 0;
    double prev_ = 0.0;
};

} // namespace wers
