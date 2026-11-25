#pragma once
// The tube amplifier after the gain cell, run at the oversampled rate.
#include <cmath>

namespace lampa {

// What the tube stage needs, worked out every few samples from Kolor and the character.
struct TubeShape {
    float invClip = 0.0f;  // 1 / the level where the tube starts to bend
    float clip = 1.0f;
    float bias = 0.0f;     // asymmetry: even harmonics
    float tanhBias = 0.0f, gainNorm = 1.0f;
    float iron = 0.0f;     // how much the transformer's low end saturates
    float ironClip = 1.0f, invIronClip = 1.0f;
    float ironLowpass = 0.0f;
    float squeezeDrive = 0.0f; // extra drive per dB of gain reduction (tube character)

    // color 0..1, tube 0 optical .. 1 tube, fs the oversampled rate.
    static TubeShape make(float color, float tube, double fs) {
        TubeShape s;
        // Kolor sets the headroom: at 0 the bend starts 36 dB above full scale, at 100% 6 dB below it.
        // The tube character drives a little harder, since its odd harmonics are quieter at the same drive.
        const float headroomDb = 36.0f - 42.0f * std::pow(color, 0.6f) - 4.0f * tube;
        s.clip = std::pow(10.0f, headroomDb / 20.0f);
        s.invClip = 1.0f / s.clip;
        // A single-ended triode leans to one side (second harmonic); push-pull is nearly symmetric (third).
        s.bias = 0.30f + tube * (0.012f - 0.30f);
        s.tanhBias = std::tanh(s.bias);
        s.gainNorm = 1.0f / (1.0f - s.tanhBias * s.tanhBias);
        s.iron = 0.15f + 0.55f * color;
        s.ironClip = s.clip * 1.6f;
        s.invIronClip = 1.0f / s.ironClip;
        s.ironLowpass = static_cast<float>(1.0 - std::exp(-2.0 * 3.14159265358979 * 110.0 / fs));
        s.squeezeDrive = 0.045f * tube;
        return s;
    }
};

// One channel. Small signals pass at unity gain; louder ones bend, more on one side than the other.
struct TubeStage {
    float low = 0.0f;
    float heat = 0.0f; // how hard the tube is driven, for the glow on screen

    void reset() { low = heat = 0.0f; }

    // drive >= 1 pushes the tube harder without changing the small-signal level.
    float process(float x, const TubeShape& s, float drive) {
        const float u = x * s.invClip * drive;
        float y = (std::tanh(u + s.bias) - s.tanhBias) * s.gainNorm * s.clip / drive;
        // The output transformer: the lows bend first, a soft thickening of loud bass.
        low += (y - low) * s.ironLowpass;
        const float bent = s.ironClip * std::tanh(low * s.invIronClip);
        y += s.iron * (bent - low);
        const float a = std::fabs(u);
        // Peaks light the glow at once; it fades over about a second.
        heat = a > heat ? a : heat * 0.999997f;
        return y;
    }
};

} // namespace lampa
