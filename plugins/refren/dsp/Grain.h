#pragma once
// Saturation on what the layers make together. It only adds what the curve changes, and it brings back the level,
// so Grain never makes the voice louder and tails never pump. At a Drive of zero it passes the sound unchanged and
// costs next to nothing.
#include "CharacterShaper.h"
#include "LevelMatch.h"
#include "Oversampler.h"
#include "Tone.h"

namespace refren {

class Grain {
public:
    void prepare(double fs);
    void reset() noexcept;
    void set(float driveDb, wers::Character character) noexcept;
    void tick(float& left, float& right) noexcept;
private:
    struct Channel {
        wers::Oversampler oversampler;
        wers::CharacterShaper shaper;
        wers::DcBlock dc;
        wers::DelayLine delay;
    };
    float shaped(Channel& channel, float x, float delayed) noexcept;

    std::array<Channel, 2> channel_;
    wers::LevelMatch match_;
    wers::Character character_ = wers::Character::Lamp;
    float driveDb_ = 0.0f, gain_ = 1.0f, gainTarget_ = 1.0f, amount_ = 0.0f, amountTarget_ = 0.0f, smooth_ = 0.001f;
    float makeup_ = 1.0f, makeupTarget_ = 1.0f;
    bool snap_ = true, running_ = false;
};

} // namespace refren
