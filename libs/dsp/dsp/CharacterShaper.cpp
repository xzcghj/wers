#include "CharacterShaper.h"
#include <bit>

namespace wers {

namespace {

constexpr double kFlatQ = 0.70710678118654752;
constexpr double kTapeShelfHz = 3000.0, kTapeShelfDb = 6.0;
constexpr double kTransformerShelfHz = 300.0, kTransformerShelfDb = 8.0;
constexpr double kLevelSeconds = 0.03;
constexpr float kLampBias = 0.25f, kLampBiasFromLevel = 0.15f;
constexpr float kTapeKnee = 2.5f;
constexpr float kInputLimit = 64.0f; // far above any real signal, keeps every power below finite

// tanh as a ratio of polynomials. Accurate to about 1e-6, then it stays at 1 exactly.
inline float softTanh(float x) noexcept {
    const float a = std::clamp(x, -8.0f, 8.0f);
    const float s = a * a;
    const float top = a * (34459425.0f + s * (4729725.0f + s * (135135.0f + s * (990.0f + s))));
    const float bottom = 34459425.0f + s * (16216200.0f + s * (945945.0f + s * (13860.0f + s * 45.0f)));
    return std::clamp(top / bottom, -1.0f, 1.0f);
}

// 2 to the power of (exponent / knee), for the root below.
const std::array<float, 128> kTapeRootOfExponent = [] {
    std::array<float, 128> table{};
    for (size_t e = 0; e < table.size(); ++e) table[e] = static_cast<float>(std::pow(2.0, static_cast<double>(e) / kTapeKnee));
    return table;
}();

// x to the power of (1 / knee), for x of at least 1, to about 5e-7. The plain power function took more
// time than the rest of the curve. The exponent of x comes from a table, its mantissa from a polynomial.
inline float tapeRoot(float x) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    const float m = std::bit_cast<float>((bits & 0x007fffffu) | 0x3f800000u);
    const float root = 0.36965585f + m * (1.0400375f + m * (-0.67946905f + m * (0.38494176f + m * (-0.14266242f + m * (0.030283660f + m * -0.0027868701f)))));
    return kTapeRootOfExponent[std::min((bits >> 23) - 127u, 127u)] * root;
}

} // namespace

void CharacterShaper::prepare(double highRate) {
    rate_ = highRate;
    envCoef_ = onePoleCoeff(kLevelSeconds, highRate);
    setCharacter(character_);
    reset();
}

void CharacterShaper::reset() noexcept {
    env_ = 0.0f;
    emphasis_.reset();
    deemphasis_.reset();
}

void CharacterShaper::setCharacter(Character character) noexcept {
    character_ = character;
    using Type = Biquad::Type;
    // A shelf with the opposite gain is the exact inverse of the first one.
    if (character == Character::Tape) {
        emphasis_.set(Type::HighShelf, kTapeShelfHz, kFlatQ, kTapeShelfDb, rate_);
        deemphasis_.set(Type::HighShelf, kTapeShelfHz, kFlatQ, -kTapeShelfDb, rate_);
    } else if (character == Character::Transformer) {
        emphasis_.set(Type::LowShelf, kTransformerShelfHz, kFlatQ, kTransformerShelfDb, rate_);
        deemphasis_.set(Type::LowShelf, kTransformerShelfHz, kFlatQ, -kTransformerShelfDb, rate_);
    }
    emphasis_.reset();
    deemphasis_.reset();
}

float CharacterShaper::tick(float x, float gain) noexcept {
    const float in = std::isfinite(x) ? std::clamp(x, -kInputLimit, kInputLimit) : 0.0f;
    const float g = std::max(gain, 1.0e-3f);
    switch (character_) {
        case Character::Lamp: {
            const float u = g * in;
            env_ += (std::min(std::abs(u), 1.0f) - env_) * static_cast<float>(envCoef_);
            const float bias = kLampBias + kLampBiasFromLevel * env_;
            const float biasTanh = softTanh(bias);
            return (softTanh(u + bias) - biasTanh) / ((1.0f - biasTanh * biasTanh) * g);
        }
        case Character::Tape: {
            const float u = g * static_cast<float>(emphasis_.tick(static_cast<double>(in)));
            const float a = std::abs(u);
            const float w = a * a * std::sqrt(a);
            const float y = u / tapeRoot(1.0f + w);
            return static_cast<float>(deemphasis_.tick(static_cast<double>(y / g)));
        }
        case Character::Transformer: {
            const float u = g * static_cast<float>(emphasis_.tick(static_cast<double>(in)));
            return static_cast<float>(deemphasis_.tick(static_cast<double>(softTanh(u) / g)));
        }
        case Character::Transistor: {
            const float u = g * in;
            const float s = u * u;
            return u / (std::sqrt(std::sqrt(1.0f + s * s)) * g);
        }
    }
    return in;
}

} // namespace wers
