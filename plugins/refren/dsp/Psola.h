#pragma once
// One stream of the voice at another pitch, another colour or on a fixed note. Short pieces of the voice, one
// for each period, are laid down at new distances, so the shape of its sound stays where it was. The stream
// plays what the input did delaySamples ago, which gives the pieces time to be read. It is silent where
// the voice has no pitch. All memory is allocated in prepare().
#include "VoiceAnalysis.h"

namespace refren {

struct PsolaSettings {
    float semitones = 0.0f;
    float formantSemitones = 0.0f;
    bool fixedNote = false;
    float noteHz = 130.81f;
};

class Psola {
public:
    void prepare(double fs, int delaySamples);
    void reset() noexcept;
    void set(const PsolaSettings& settings) noexcept;
    // Once a sample, after voice.push.
    float tick(const VoiceAnalysis& voice) noexcept;
private:
    void placeGrain(const VoiceAnalysis& voice, const Mark& mark, double halfLength, double gain, int64_t newest) noexcept;
    double fs_ = 48000.0, nextGrain_ = 0.0, spacing_ = 0.0, ratio_ = 1.0, formant_ = 1.0, noteSpacing_ = 0.0, silentStep_ = 120.0, longestHalf_ = 640.0;
    int delay_ = 0, gateLead_ = 120;
    bool fixedNote_ = false, startOfVoice_ = true;
    std::vector<float> overlap_, hann_;
    int64_t overlapMask_ = 0;
    float gate_ = 0.0f, gateStep_ = 0.004f;
};

} // namespace refren
