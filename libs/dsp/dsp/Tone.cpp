#include "Tone.h"

namespace wers {

namespace {
constexpr double kDcHz = 12.0, kTiltHz = 1000.0, kTiltDb = 6.0;
}

void DcBlock::prepare(double fs) noexcept {
    pole_ = 1.0 - 2.0 * kPi * kDcHz / fs;
    gain_ = 0.5 * (1.0 + pole_);
    reset();
}

void Tilt::prepare(double fs) noexcept {
    warp_ = std::tan(kPi * kTiltHz / fs);
    set(0.0f);
    reset();
}

void Tilt::set(float tone) noexcept {
    const double k = std::pow(10.0, kTiltDb * static_cast<double>(tone) / 20.0);
    const double denominator = 1.0 + k * warp_;
    b0_ = (k + warp_) / denominator;
    b1_ = (warp_ - k) / denominator;
    a1_ = (k * warp_ - 1.0) / denominator;
}

} // namespace wers
