#pragma once
// A room for the voice: short all-pass filters smear what comes in, and eight delay lines of lengths that share no
// factor keep passing it to each other through a mixing matrix that loses no energy. Each line takes off its share of
// the level and of the top, and drifts by half a millisecond, so no note rings on its own. Shimmer lifts a part of
// what circulates by an octave, always in place of it, so it cannot add energy to the loop.
#include "Common.h"
#include <vector>

namespace refren {

class Reverb {
public:
    void prepare(double fs);
    void reset() noexcept;
    // size 0 to 1, decay in seconds to fall 60 dB, tone in Hz, predelay up to 80 ms, shimmer 0 to 1.
    void set(float size, float decaySeconds, float toneHz, float preMs, float shimmer) noexcept;
    void tick(float inL, float inR, float& outL, float& outR) noexcept;
private:
    static constexpr int kLines = 8, kDiffusers = 4;
    struct Line {
        std::vector<float> data;
        int mask = 0, position = 0, length = 1;
        float gain = 1.0f, dampCoef = 1.0f, damp = 0.0f;
        double sine = 0.0, cosine = 1.0, step = 0.0;
    };
    struct Diffuser {
        std::vector<float> data;
        int position = 0;
        float tick(float x) noexcept;
    };
    // Reads at twice the speed in two overlapping pieces, so the pitch goes up an octave.
    struct OctaveUp {
        std::vector<float> data;
        int position = 0;
        float low = 0.0f;
    };
    void updateLengths() noexcept;
    void updateGains() noexcept;
    void updateDamping() noexcept;
    float shifted(OctaveUp& shifter, float x) noexcept;

    double fs_ = 48000.0;
    std::array<Line, kLines> line_;
    std::array<std::array<Diffuser, kDiffusers>, 2> diffuser_;
    std::array<std::vector<float>, 2> pre_;
    std::array<OctaveUp, 2> octave_;
    std::vector<float> window_;
    int preSize_ = 1, prePosition_ = 0, piece_ = 1, pieceAt_ = 0;
    double preNow_ = 0.0, preWanted_ = 0.0, preGlide_ = 0.001;
    float size_ = -1.0f, decay_ = -1.0f, tone_ = -1.0f, swing_ = 1.0f;
    float shimmer_ = 0.0f, shimmerTarget_ = 0.0f, smooth_ = 0.001f, octaveCoef_ = 0.5f;
    bool snap_ = true, shimmering_ = false;
};

} // namespace refren
