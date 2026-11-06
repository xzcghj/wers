#pragma once
// One kind of movement on the whole wet sound. At a depth of zero it changes nothing.
#include "RefrenParams.h"
#include <vector>

namespace refren {

class MotionFx {
public:
    void prepare(double fs);
    void reset() noexcept;
    void set(Motion kind, float rateHz, float depth) noexcept;
    void tick(float& left, float& right) noexcept;
private:
    float flange(int channel, float x, float swing, float feedback, float mix, float level) noexcept;

    double fs_ = 48000.0;
    Motion kind_ = Motion::None;
    float rateHz_ = 0.0f, depth_ = 0.0f, depthTarget_ = 0.0f, smooth_ = 0.001f;
    double step_ = 0.0, sine_ = 0.0, cosine_ = 1.0;
    bool snap_ = true;
    std::array<std::vector<float>, 2> delay_;
    int size_ = 1, position_ = 0;
    float centre_ = 1.0f, reach_ = 0.0f;
};

} // namespace refren
