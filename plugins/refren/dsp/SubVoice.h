#pragma once
// A clean tone under the voice, following its pitch and its level. Silent where the voice has no pitch.
#include "VoiceAnalysis.h"

namespace refren {

class SubVoice {
public:
    void prepare(double fs, int delaySamples);
    void reset() noexcept;
    // Once a sample, after voice.push. Plays what the input did delaySamples ago.
    float tick(const VoiceAnalysis& voice, float semitones) noexcept;
private:
    double fs_ = 48000.0, phase_ = 0.0, hz_ = 0.0, hzCoef_ = 0.004;
    int delay_ = 0;
    float gate_ = 0.0f, gateStep_ = 0.004f;
};

} // namespace refren
