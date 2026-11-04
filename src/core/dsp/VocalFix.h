#pragma once
// Applies the vocal half of word fixes on the project timeline, and notices when the vocal no
// longer matches the one that was analysed.
#include "Filters.h"
#include "link/Plan.h"

namespace wers {

class VocalFix {
public:
    void prepare(double fs);
    void reset() noexcept;
    // timeline: sample position of the audio that is being processed here, already corrected for
    // latency. Negative or not playing means no fixes.
    void process(float* const* ch, int numChannels, int numSamples, int64_t timeline, bool playing, const Plan* plan, bool enabled) noexcept;
private:
    double fs_ = 48000.0;
    int controlEvery_ = 32, controlCount_ = 0;
    std::array<std::array<Svf, kBoostBands>, 2> bell_{};
    std::array<double, kBoostBands> gain_{}, step_{}, target_{};
};

// Compares the live voice level with the level track of the analysed vocal.
class StaleDetector {
public:
    void prepare(double fs);
    void reset() noexcept;
    // Feed once per block with the current voice level and the time of the audio it belongs to.
    void feed(double timeSec, float levelDb, const Plan* plan) noexcept;
    bool stale() const noexcept { return stale_; }
private:
    static constexpr int kWindow = 300; // 3 s of 10 ms frames
    void evaluate() noexcept;
    std::array<float, kWindow> live_{}, stored_{};
    int fill_ = 0;
    int64_t lastFrame_ = -1;
    int badWindows_ = 0;
    bool stale_ = false;
};

} // namespace wers
