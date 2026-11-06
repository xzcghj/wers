#include "Grain.h"

namespace refren {

namespace {
constexpr double kSmoothSeconds = 0.02;
constexpr int kFactorLog2 = 1;
constexpr float kSnap = 1.0e-5f;
// Past this, a vocal at any usual level sits on the flat part of the curve and every dB of Drive takes a dB off it.
// Giving that back right away keeps what the slow level match has to do small, so a fast move of Drive does not jump.
constexpr float kFlatFromDb = 22.0f;
}

void Grain::prepare(double fs) {
    for (auto& c : channel_) {
        c.oversampler.prepare(kFactorLog2);
        c.shaper.prepare(fs * c.oversampler.factor());
        c.dc.prepare(fs);
        c.delay.prepare(c.oversampler.latencySamples());
    }
    match_.prepare(fs);
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, fs));
    reset();
}

void Grain::reset() noexcept {
    for (auto& c : channel_) {
        c.oversampler.reset();
        c.shaper.reset();
        c.dc.reset();
        c.delay.reset();
    }
    match_.reset();
    snap_ = true;
    running_ = false;
}

void Grain::set(float driveDb, wers::Character character) noexcept {
    if (character != character_) {
        character_ = character;
        for (auto& c : channel_) c.shaper.setCharacter(character);
    }
    if (driveDb != driveDb_) {
        driveDb_ = driveDb;
        gainTarget_ = wers::dbToGain(driveDb);
        amountTarget_ = wers::differenceAmount(driveDb);
        makeupTarget_ = wers::dbToGain(std::max(0.0f, driveDb - kFlatFromDb));
    }
    if (snap_) {
        gain_ = gainTarget_;
        amount_ = amountTarget_;
        makeup_ = makeupTarget_;
        snap_ = false;
    }
}

float Grain::shaped(Channel& channel, float x, float delayed) noexcept {
    std::array<float, 2> frame{};
    channel.oversampler.up(x, frame.data());
    for (auto& v : frame) v = channel.shaper.tick(v, gain_);
    const float changed = channel.oversampler.down(frame.data());
    return static_cast<float>(channel.dc.tick(static_cast<double>(changed - delayed)));
}

void Grain::tick(float& left, float& right) noexcept {
    const float inLeft = left, inRight = right;
    left = channel_[0].delay.process(inLeft);
    right = channel_[1].delay.process(inRight);
    if (amount_ == 0.0f && amountTarget_ == 0.0f) {
        running_ = false;
        return;
    }
    if (!running_) {
        // Whatever these held is from long ago.
        for (auto& c : channel_) {
            c.oversampler.reset();
            c.shaper.reset();
            c.dc.reset();
        }
        match_.reset();
        running_ = true;
    }
    gain_ += (gainTarget_ - gain_) * smooth_;
    makeup_ += (makeupTarget_ - makeup_) * smooth_;
    amount_ += (amountTarget_ - amount_) * smooth_;
    if (std::abs(amountTarget_ - amount_) < kSnap) amount_ = amountTarget_;
    const float wetLeft = (left + amount_ * shaped(channel_[0], inLeft, left)) * makeup_;
    const float wetRight = (right + amount_ * shaped(channel_[1], inRight, right)) * makeup_;
    const float match = match_.push(left, right, wetLeft, wetRight);
    const float applied = 1.0f + amount_ * (match - 1.0f);
    left = wetLeft * applied;
    right = wetRight * applied;
}

} // namespace refren
