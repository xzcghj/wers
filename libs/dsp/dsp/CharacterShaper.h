#pragma once
// The four characters of saturation in the series. Each models how a kind of circuit behaves; none is a copy of a device.
// Small signals pass with a gain of 1, so Drive only decides how far up the curve a signal reaches.
// Runs at the oversampled rate, one shaper per band and channel.
#include "Filters.h"

namespace wers {

enum class Character : int { Lamp = 0, Tape = 1, Transformer = 2, Transistor = 3 };

// How much of the difference a shaper makes is used. It grows in the first 3 dB of Drive, so a Drive of
// zero is exactly no change.
inline float differenceAmount(float driveDb) noexcept {
    const float t = std::clamp(driveDb / 3.0f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

class CharacterShaper {
public:
    void prepare(double highRate);
    void reset() noexcept;
    void setCharacter(Character character) noexcept;
    // gain is Drive as a linear value. Input that is not a number counts as silence.
    float tick(float x, float gain) noexcept;
private:
    double rate_ = 96000.0, envCoef_ = 0.001;
    Character character_ = Character::Lamp;
    float env_ = 0.0f;
    Biquad emphasis_, deemphasis_;
};

} // namespace wers
