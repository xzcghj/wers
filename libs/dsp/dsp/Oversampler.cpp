#include "Oversampler.h"

namespace wers {

namespace {

constexpr double kKaiserBeta = 10.06; // about 100 dB in the stop band

double besselI0(double x) noexcept {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64 && term > 1.0e-18 * sum; ++k) {
        term *= (x * 0.5 / k) * (x * 0.5 / k);
        sum += term;
    }
    return sum;
}

float dotProduct(const float* a, const float* b, int n) noexcept {
    float sum = 0.0f;
    int i = 0;
#if WERS_HAS_SSE
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
    float parts[4];
    _mm_storeu_ps(parts, acc);
    sum = parts[0] + parts[1] + parts[2] + parts[3];
#endif
    for (; i < n; ++i) sum += a[i] * b[i];
    return sum;
}

} // namespace

int effectiveFactorLog2(int wanted, double fs) noexcept {
    int log2 = std::clamp(wanted, 1, 3);
    while (log2 > 1 && fs * static_cast<double>(1 << log2) > 384000.5) --log2;
    return log2;
}

void HalfbandStage::prepare(int taps) {
    const int centre = (taps - 1) / 2;
    side_ = (taps + 1) / 2;
    upCentre_ = (centre - 1) / 2;
    centreDelay_ = (centre + 1) / 2;
    double sum = 0.0;
    std::array<double, kMaxSide> side{};
    const double windowScale = besselI0(kKaiserBeta);
    for (int i = 0; i < side_; ++i) {
        const double n = 2.0 * i - centre; // always odd, the even distances are the zero taps
        const double t = n / centre;
        const double window = besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - t * t))) / windowScale;
        side[static_cast<size_t>(i)] = std::sin(kPi * n * 0.5) / (kPi * n) * window;
        sum += side[static_cast<size_t>(i)];
    }
    // The side taps add up to exactly 0.5, so DC passes unchanged.
    for (int i = 0; i < side_; ++i) coef_[static_cast<size_t>(i)] = static_cast<float>(side[static_cast<size_t>(i)] * 0.5 / sum);
    reset();
}

void HalfbandStage::reset() noexcept {
    upHist_.fill(0.0f);
    downHist_.fill(0.0f);
    centreHist_.fill(0.0f);
    upPos_ = downPos_ = centrePos_ = 0;
}

void HalfbandStage::up(float x, float& first, float& second) noexcept {
    upPos_ = upPos_ == 0 ? side_ - 1 : upPos_ - 1;
    upHist_[static_cast<size_t>(upPos_)] = x;
    upHist_[static_cast<size_t>(upPos_ + side_)] = x;
    const float* newestFirst = upHist_.data() + upPos_;
    first = 2.0f * dotProduct(coef_.data(), newestFirst, side_);
    second = newestFirst[upCentre_];
}

float HalfbandStage::down(float first, float second) noexcept {
    downPos_ = downPos_ == 0 ? side_ - 1 : downPos_ - 1;
    downHist_[static_cast<size_t>(downPos_)] = first;
    downHist_[static_cast<size_t>(downPos_ + side_)] = first;
    const float sides = dotProduct(coef_.data(), downHist_.data() + downPos_, side_);
    const float centre = centreHist_[static_cast<size_t>(centrePos_)];
    centreHist_[static_cast<size_t>(centrePos_)] = second;
    if (++centrePos_ >= centreDelay_) centrePos_ = 0;
    return sides + 0.5f * centre;
}

void Oversampler::prepare(int factorLog2) {
    stages_ = std::clamp(factorLog2, 1, 3);
    stage_[0].prepare(99);
    stage_[1].prepare(31);
    stage_[2].prepare(19);
    // Stage delays are 49, 7.5 and 2.25 base samples. One extra sample inside the cascade makes
    // the total whole: at the 2x level for 4x, at the 4x level for 8x.
    latency_ = stages_ == 1 ? 49 : (stages_ == 2 ? 57 : 59);
    reset();
}

void Oversampler::reset() noexcept {
    for (auto& s : stage_) s.reset();
    align_ = 0.0f;
}

void Oversampler::up(float x, float* out) noexcept {
    float a = 0.0f, b = 0.0f;
    stage_[0].up(x, a, b);
    if (stages_ == 1) {
        out[0] = a;
        out[1] = b;
        return;
    }
    float m[4];
    stage_[1].up(a, m[0], m[1]);
    stage_[1].up(b, m[2], m[3]);
    if (stages_ == 2) {
        for (int i = 0; i < 4; ++i) out[i] = m[i];
        return;
    }
    for (int i = 0; i < 4; ++i) stage_[2].up(m[i], out[2 * i], out[2 * i + 1]);
}

float Oversampler::down(const float* in) noexcept {
    if (stages_ == 1) return stage_[0].down(in[0], in[1]);
    if (stages_ == 2) {
        const float a = align_;
        const float b = stage_[1].down(in[0], in[1]);
        align_ = stage_[1].down(in[2], in[3]);
        return stage_[0].down(a, b);
    }
    float m[4];
    for (int i = 0; i < 4; ++i) {
        m[i] = align_;
        align_ = stage_[2].down(in[2 * i], in[2 * i + 1]);
    }
    const float a = stage_[1].down(m[0], m[1]);
    const float b = stage_[1].down(m[2], m[3]);
    return stage_[0].down(a, b);
}

} // namespace wers
