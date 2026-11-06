#pragma once
// The voices of one layer: readings of one stream at different moments, spread across the stereo image, each
// slowly drifting in pitch and with a level and a colour of its own. Memory for all voices is set in prepare().
#include "RefrenParams.h"
#include "Smoothing.h"

namespace refren {

class Layer {
public:
    static constexpr int kMaxVoices = 16;
    // Where a voice stands in a layer of this size, from -1 at the far left to 1 at the far right.
    static float panShareOf(int index, int count) noexcept;
    void prepare(double fs, uint32_t seed);
    void reset() noexcept;
    // Once per control step. Level, width, time and detune glide to their new values in 20 ms, except right after a
    // reset. The number of voices takes effect at once, so the engine changes it only when nothing is heard.
    void set(const LayerParams& params, float widthScale, float timeScale, int allowedVoices, int streamDelay) noexcept;
    // Once a sample: takes the next sample of the stream and adds this layer to left and right.
    void tick(float stream, float& left, float& right) noexcept;
    int activeVoices() const noexcept { return active_; }
    float levelGain() const noexcept { return levelGain_; }
    int controlStep() const noexcept { return controlStep_; }
private:
    struct Voice {
        float offsetShare = 1.0f, panShare = 0.0f, ownGain = 1.0f, highGain = 1.0f, rateHz = 0.5f;
        float phase = 0.0f, from = 0.0f, to = 0.0f, lowState = 0.0f, arrival = 0.0f;
        uint32_t random = 1u;
        wers::Ramp delay, left, right;
    };
    float nextRandom(uint32_t& state) const noexcept;
    void placeVoices(int count, bool startsAtZero) noexcept;
    double fs_ = 48000.0;
    uint32_t seed_ = 1u;
    int latency_ = 1440, shortestRead_ = 2, controlStep_ = 32;
    float perSample_ = 1.0f / 32.0f, smooth_ = 0.05f, arrivalSmooth_ = 0.1f, secondsPerStep_ = 32.0f / 48000.0f, shelfCoef_ = 0.28f;
    std::array<Voice, kMaxVoices> voice_{};
    std::vector<float> line_;
    int lineMask_ = 0, writePos_ = 0, active_ = 0, written_ = 0;
    bool first_ = true, startsAtZero_ = false;
    float levelDb_ = -6.0f, width_ = 0.5f, timeMs_ = 12.0f, detune_ = 8.0f, levelGain_ = 0.0f;
};

} // namespace refren
