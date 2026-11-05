#pragma once
// The four characters of Barwa. Each models how a kind of circuit behaves; none is a copy of a device.
// Small signals pass with a gain of 1, so Drive only decides how far up the curve a signal reaches.
// Runs at the oversampled rate, one shaper per band and channel.
#include "Filters.h"

namespace barwa {

enum class Character : int { Lamp = 0, Tape = 1, Transformer = 2, Transistor = 3 };

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
    wers::Biquad emphasis_, deemphasis_;
};

} // namespace barwa
