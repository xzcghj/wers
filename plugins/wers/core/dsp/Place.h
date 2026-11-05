#pragma once
// Miejsce: makes room for the vocal in the middle of the beat. Sub, kick and sides stay untouched.
#include "Filters.h"

namespace wers {

// Everything the beat needs to know about the vocal at a point on the timeline.
class PlaceControl {
public:
    virtual ~PlaceControl() = default;
    // Vocal level in each band at the time, in dB. Returns false when nothing is known.
    virtual bool vocalBands(double timeSec, float* levelsDb) const noexcept = 0;
    // Extra cut from word fixes at the time, in dB (positive). Returns false when none applies.
    virtual bool fixCuts(double timeSec, float* cutsDb) const noexcept = 0;
};

class Place {
public:
    void prepare(double fs);
    void reset() noexcept;
    void setDepth(float depth) noexcept { depth_ = clamp01(depth); }
    // timeline: sample position of the block start, or negative when the transport is unknown.
    void process(float* const* ch, int numChannels, int numSamples,
                 const float* const* sidechain, int numSidechain,
                 int64_t timeline, bool playing, const PlaceControl* control) noexcept;
    void currentCutsDb(float* out) const noexcept { for (size_t i = 0; i < kCutBands; ++i) out[i] = cutDb_[i]; }
    // Same rule the score model uses for the live cut in one band.
    static float liveCutDb(float vocalDb, float beatDb, int band, float depth) noexcept;
private:
    void updateControls(double timeSec, bool playing, const PlaceControl* control, bool haveSide) noexcept;

    double fs_ = 48000.0;
    float depth_ = 0.0f;
    int controlEvery_ = 64, controlCount_ = 0;
    std::array<Svf, kCutBands> bell_{};
    BandEnvelope beatEnv_, sideEnv_;
    std::array<float, kCutBands> cutDb_{};
    std::array<double, kCutBands> gain_{}, gainStep_{};
    std::array<float, kCutBands> levels_{}, beatLevels_{}, fix_{};
    float att_ = 0.2f, rel_ = 0.02f;
    int64_t samplesSinceSide_ = 1LL << 40;
};

} // namespace wers
