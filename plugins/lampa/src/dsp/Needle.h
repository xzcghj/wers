#pragma once
// The mechanics of a VU meter's needle: a coil on a spring with a little damping. It reaches 99% of a
// step in about 300 ms and swings past it by about 1,5%, like the real thing.
#include <algorithm>
#include <cmath>

namespace lampa {

struct Needle {
    static constexpr float kOmega = 13.5f;  // rad/s
    static constexpr float kDamping = 0.80f;
    float position = 0.0f, velocity = 0.0f;

    // Moves towards target over dt seconds, in small steps so a slow frame rate does not change the swing.
    void step(float target, float dt) {
        dt = std::clamp(dt, 0.0f, 0.25f);
        const int n = std::max(1, static_cast<int>(std::ceil(dt / 0.001f)));
        const float h = dt / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const float accel = kOmega * kOmega * (target - position) - 2.0f * kDamping * kOmega * velocity;
            velocity += accel * h;
            position += velocity * h;
        }
    }
};

// Where the needle points, 0..1 across the scale, for a level in VU. A VU meter moves with voltage, so the
// scale is crowded at the bottom: -20 VU sits near the left end, 0 VU at about 71%, +3 VU at the right end.
inline float vuDeflection(float vu) { return std::pow(10.0f, (std::min(vu, 6.0f) - 3.0f) / 20.0f); }
inline float vuFromDeflection(float d) { return 20.0f * std::log10(std::max(d, 1e-6f)) + 3.0f; }

} // namespace lampa
