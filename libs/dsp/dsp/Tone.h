#pragma once
// Small tone tools for what saturation adds.
#include "Common.h"

namespace wers {

// Takes the offset of asymmetric curves out of what is added.
class DcBlock {
public:
    void prepare(double fs) noexcept;
    void reset() noexcept { in_ = out_ = 0.0; }
    double tick(double x) noexcept {
        out_ = gain_ * (x - in_) + pole_ * out_;
        in_ = x;
        return out_;
    }
private:
    double pole_ = 0.998, gain_ = 0.999, in_ = 0.0, out_ = 0.0;
};

// Tilts the sound around 1 kHz, up to 6 dB each way. At zero it changes nothing.
class Tilt {
public:
    void prepare(double fs) noexcept;
    void reset() noexcept { state_ = 0.0; }
    void set(float tone) noexcept;
    double tick(double x) noexcept {
        const double y = b0_ * x + state_;
        state_ = b1_ * x - a1_ * y;
        return y;
    }
private:
    double warp_ = 0.07, b0_ = 1.0, b1_ = 0.0, a1_ = 0.0, state_ = 0.0;
};

} // namespace wers
