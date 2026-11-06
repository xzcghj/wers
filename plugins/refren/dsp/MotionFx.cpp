#include "MotionFx.h"

namespace refren {

namespace {
constexpr double kSmoothSeconds = 0.02, kFlangerCentreSeconds = 0.0035, kFlangerReachSeconds = 0.0025;
constexpr float kFlangerFeedback = 0.7f, kSnap = 1.0e-5f;
}

void MotionFx::prepare(double fs) {
    fs_ = fs;
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, fs));
    centre_ = static_cast<float>(kFlangerCentreSeconds * fs);
    reach_ = static_cast<float>(kFlangerReachSeconds * fs);
    size_ = static_cast<int>(std::ceil(centre_ + reach_)) + 4;
    for (auto& d : delay_) d.assign(static_cast<size_t>(size_), 0.0f);
    rateHz_ = 0.0f;
    reset();
}

void MotionFx::reset() noexcept {
    for (auto& d : delay_) std::fill(d.begin(), d.end(), 0.0f);
    position_ = 0;
    sine_ = 0.0;
    cosine_ = 1.0;
    snap_ = true;
}

void MotionFx::set(Motion kind, float rateHz, float depth) noexcept {
    if (kind != kind_) {
        kind_ = kind;
        for (auto& d : delay_) std::fill(d.begin(), d.end(), 0.0f);
    }
    if (rateHz != rateHz_) {
        rateHz_ = rateHz;
        step_ = 2.0 * wers::kPi * static_cast<double>(rateHz) / fs_;
    }
    depthTarget_ = depth;
    if (snap_) {
        depth_ = depth;
        snap_ = false;
    }
}

float MotionFx::flange(int channel, float x, float swing, float feedback, float mix, float level) noexcept {
    auto& line = delay_[static_cast<size_t>(channel)];
    float place = static_cast<float>(position_) - (centre_ + reach_ * swing);
    if (place < 0.0f) place += static_cast<float>(size_);
    int first = static_cast<int>(place);
    const float part = place - static_cast<float>(first);
    if (first >= size_) first -= size_;
    const int second = first + 1 >= size_ ? 0 : first + 1;
    const float delayed = line[static_cast<size_t>(first)] + (line[static_cast<size_t>(second)] - line[static_cast<size_t>(first)]) * part;
    line[static_cast<size_t>(position_)] = x + feedback * delayed;
    return (x + mix * delayed) * level;
}

void MotionFx::tick(float& left, float& right) noexcept {
    if (kind_ == Motion::None) return;
    // A rotation that keeps its size for as long as it runs.
    sine_ += step_ * cosine_;
    cosine_ -= step_ * sine_;
    depth_ += (depthTarget_ - depth_) * smooth_;
    if (std::abs(depthTarget_ - depth_) < kSnap) depth_ = depthTarget_;
    switch (kind_) {
        case Motion::None:
            break;
        case Motion::Autopan: {
            // The powers of the two sides always add up to the same.
            const float place = depth_ * static_cast<float>(sine_);
            left *= std::sqrt(std::max(0.0f, 1.0f - place));
            right *= std::sqrt(std::max(0.0f, 1.0f + place));
            break;
        }
        case Motion::Tremolo: {
            const float gain = 1.0f - depth_ * (0.5f + 0.5f * static_cast<float>(sine_));
            left *= gain;
            right *= gain;
            break;
        }
        case Motion::Flanger: {
            const float feedback = kFlangerFeedback * depth_;
            // Keeps the power of noise the same at every depth.
            const float level = 1.0f / std::sqrt(1.0f + depth_ * depth_ / (1.0f - feedback * feedback));
            left = flange(0, left, static_cast<float>(sine_), feedback, depth_, level);
            right = flange(1, right, static_cast<float>(cosine_), feedback, depth_, level);
            if (++position_ >= size_) position_ = 0;
            break;
        }
    }
}

} // namespace refren
