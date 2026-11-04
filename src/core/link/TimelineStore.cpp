#include "TimelineStore.h"

namespace wers {

void TimelineStore::prepare(double hostRate) noexcept {
    disarm();
    hostRate_ = hostRate;
    ratio_ = kAnalysisRate / hostRate;
    const double fc = std::min(7600.0, hostRate * 0.45);
    static constexpr std::array<double, 3> q{ 0.5176, 0.7071, 1.9319 };
    for (size_t i = 0; i < 3; ++i) lp_[i].set(Biquad::Type::LowPass, fc, q[i], 0.0, hostRate);
    expected_ = -1;
}

bool TimelineStore::arm() {
    disarm();
    if (data_.empty()) data_.resize(capacity_, 0.0f);
    else std::fill(data_.begin(), data_.end(), 0.0f);
    heardSamples_.store(0);
    endIndex_.store(0);
    expected_ = -1;
    armed_.store(true);
    return true;
}

void TimelineStore::disarm() noexcept {
    armed_.store(false);
    while (writers_.load() != 0) { /* the writer is inside capture() for a few microseconds at most */ }
}

inline void TimelineStore::writeOne(double sample, double p) noexcept {
    // p is the host sample position of this input sample.
    double y = sample;
    for (auto& s : lp_) y = s.tick(y);
    const double hi = p * ratio_;
    while (static_cast<double>(nextOut_) <= hi) {
        const double fr = (static_cast<double>(nextOut_) - (p - 1.0) * ratio_) / ratio_;
        const double out = prev_ + (y - prev_) * fr;
        if (nextOut_ >= 0 && nextOut_ < static_cast<int64_t>(capacity_)) {
            data_[static_cast<size_t>(nextOut_)] = static_cast<float>(out);
            heardSamples_.fetch_add(1, std::memory_order_relaxed);
            if (nextOut_ + 1 > endIndex_.load(std::memory_order_relaxed)) endIndex_.store(nextOut_ + 1, std::memory_order_relaxed);
        }
        ++nextOut_;
    }
    prev_ = y;
}

void TimelineStore::capture(const float* mono, int n, int64_t timeline) noexcept {
    writers_.fetch_add(1);
    if (armed_.load() && !data_.empty() && timeline >= 0) {
        if (timeline != expected_) { // a jump or the first block
            for (auto& s : lp_) s.reset();
            prev_ = 0.0;
            nextOut_ = static_cast<int64_t>(std::ceil(static_cast<double>(timeline) * ratio_));
        }
        for (int i = 0; i < n; ++i) writeOne(mono[i], static_cast<double>(timeline + i));
        expected_ = timeline + n;
    } else {
        expected_ = -1;
    }
    writers_.fetch_sub(1);
}

void TimelineStore::captureStereo(const float* l, const float* r, int n, int64_t timeline) noexcept {
    writers_.fetch_add(1);
    if (armed_.load() && !data_.empty() && timeline >= 0) {
        if (timeline != expected_) {
            for (auto& s : lp_) s.reset();
            prev_ = 0.0;
            nextOut_ = static_cast<int64_t>(std::ceil(static_cast<double>(timeline) * ratio_));
        }
        for (int i = 0; i < n; ++i) writeOne(0.5 * (static_cast<double>(l[i]) + r[i]), static_cast<double>(timeline + i));
        expected_ = timeline + n;
    } else {
        expected_ = -1;
    }
    writers_.fetch_sub(1);
}

std::vector<float> TimelineStore::copy() const {
    if (data_.empty()) return {};
    const auto end = static_cast<size_t>(std::clamp<int64_t>(endIndex_.load(), 0, static_cast<int64_t>(data_.size())));
    return std::vector<float>(data_.begin(), data_.begin() + static_cast<long>(end));
}

} // namespace wers
