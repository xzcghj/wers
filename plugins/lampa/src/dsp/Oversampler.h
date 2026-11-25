#pragma once
// Linear-phase oversampling for the tube stage. Every 2x stage is a Kaiser-windowed halfband FIR, so the
// round trip has a whole number of samples of delay and the dry signal can be lined up exactly for Mix.
#include <algorithm>
#include <array>
#include <cmath>

namespace lampa {

inline constexpr double kPi = 3.14159265358979323846;

namespace detail {

inline double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) {
        term *= (x * 0.5 / k) * (x * 0.5 / k);
        sum += term;
    }
    return sum;
}

} // namespace detail

// N = 4m + 3 taps. With the centre at an odd index, every even index holds a sinc tap and every odd one
// is zero except the centre (exactly 0.5), so each direction costs half the taps.
template <int N>
struct HalfbandTaps {
    static_assert(N >= 7 && (N - 3) % 4 == 0, "halfband length must be 4m + 3");
    static constexpr int kCentre = (N - 1) / 2;
    static constexpr int kBranch = (N + 1) / 2;   // taps at indices 0, 2, ..., N - 1
    static constexpr int kMid = (kCentre - 1) / 2; // where the centre tap sits in the other branch
    std::array<float, kBranch> taps{};

    explicit HalfbandTaps(double beta) {
        std::array<double, kBranch> h{};
        double sum = 0.0;
        for (int j = 0; j < kBranch; ++j) {
            const int i = 2 * j;
            const double off = static_cast<double>(i - kCentre);
            const double r = off / kCentre;
            const double window = detail::besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / detail::besselI0(beta);
            h[static_cast<size_t>(j)] = std::sin(kPi * off * 0.5) / (kPi * off) * window;
            sum += h[static_cast<size_t>(j)];
        }
        // The sinc taps sum to one half, the centre is the other half: unity at DC, exact halfband.
        for (int j = 0; j < kBranch; ++j) taps[static_cast<size_t>(j)] = static_cast<float>(h[static_cast<size_t>(j)] * 0.5 / sum);
    }
};

// One 2x stage for one channel: up() turns one sample into two, down() turns two into one.
// Up and down together delay by kCentre samples of the slower rate.
template <int N>
class Halfband {
public:
    using Taps = HalfbandTaps<N>;
    static constexpr int kDelay = Taps::kCentre;

    explicit Halfband(const Taps& taps) : t_(taps.taps) {}

    void reset() {
        up_.fill(0.0f);
        even_.fill(0.0f);
        odd_.fill(0.0f);
        upPos_ = evenPos_ = oddPos_ = 0;
    }

    void up(float x, float* out) {
        upPos_ = upPos_ == 0 ? kB - 1 : upPos_ - 1;
        up_[static_cast<size_t>(upPos_)] = up_[static_cast<size_t>(upPos_ + kB)] = x;
        const float* w = up_.data() + upPos_;
        float acc = 0.0f;
        for (int j = 0; j < kB; ++j) acc += t_[static_cast<size_t>(j)] * w[j];
        out[0] = 2.0f * acc;
        out[1] = w[Taps::kMid];
    }

    float down(const float* in) {
        evenPos_ = evenPos_ == 0 ? kB - 1 : evenPos_ - 1;
        even_[static_cast<size_t>(evenPos_)] = even_[static_cast<size_t>(evenPos_ + kB)] = in[0];
        const float* e = even_.data() + evenPos_;
        float acc = 0.0f;
        for (int j = 0; j < kB; ++j) acc += t_[static_cast<size_t>(j)] * e[j];
        // The centre tap reads the odd sample from before this pair: odd[n - m - 1].
        const float* o = odd_.data() + oddPos_;
        acc += 0.5f * o[Taps::kMid];
        oddPos_ = oddPos_ == 0 ? kO - 1 : oddPos_ - 1;
        odd_[static_cast<size_t>(oddPos_)] = odd_[static_cast<size_t>(oddPos_ + kO)] = in[1];
        return acc;
    }

private:
    static constexpr int kB = Taps::kBranch;
    static constexpr int kO = Taps::kMid + 2;
    const std::array<float, Taps::kBranch>& t_;
    std::array<float, 2 * kB> up_{}, even_{};
    std::array<float, 2 * kO> odd_{};
    int upPos_ = 0, evenPos_ = 0, oddPos_ = 0;
};

// Filters: the first stage has the steep edge just above the audio band, the later ones only have to keep
// their images far above it, so they are short.
using StageA = Halfband<83>;
using StageB = Halfband<35>;
using StageC = Halfband<19>;

inline const StageA::Taps& tapsA() { static const StageA::Taps t(10.0); return t; }
inline const StageB::Taps& tapsB() { static const StageB::Taps t(10.0); return t; }
inline const StageC::Taps& tapsC() { static const StageC::Taps t(10.0); return t; }

// Runs a nonlinear function at 2, 4 or 8 times the rate, for one channel. A short delay at the fast rate
// rounds the total delay up to whole samples of the outer rate.
class Oversampler {
public:
    static constexpr int kMaxFactor = 8;

    Oversampler() : a_(tapsA()), b_(tapsB()), c_(tapsC()) {}

    void setFactor(int factor) {
        factor_ = factor >= 8 ? 8 : (factor >= 4 ? 4 : 2);
        // Delay of each stage pair, in samples of the fastest rate.
        int total = StageA::kDelay * factor_;
        if (factor_ >= 4) total += StageB::kDelay * factor_ / 2;
        if (factor_ >= 8) total += StageC::kDelay * factor_ / 4;
        pad_ = (factor_ - total % factor_) % factor_;
        latency_ = (total + pad_) / factor_;
        reset();
    }

    int factor() const { return factor_; }
    int latency() const { return latency_; }

    void reset() {
        a_.reset();
        b_.reset();
        c_.reset();
        pad_buf_.fill(0.0f);
        padPos_ = 0;
    }

    template <class F>
    float process(float x, F&& f) {
        float s2[2], s4[2], s8[2], d4[2], d2[2];
        a_.up(x, s2);
        for (int i = 0; i < 2; ++i) {
            if (factor_ == 2) {
                d2[i] = delay(f(s2[i]));
                continue;
            }
            b_.up(s2[i], s4);
            for (int k = 0; k < 2; ++k) {
                if (factor_ == 4) {
                    d4[k] = delay(f(s4[k]));
                    continue;
                }
                c_.up(s4[k], s8);
                float d8[2] = { delay(f(s8[0])), delay(f(s8[1])) };
                d4[k] = c_.down(d8);
            }
            d2[i] = b_.down(d4);
        }
        return a_.down(d2);
    }

private:
    float delay(float x) {
        if (pad_ == 0) return x;
        pad_buf_[static_cast<size_t>(padPos_)] = x;
        padPos_ = (padPos_ + 1) % (pad_ + 1);
        return pad_buf_[static_cast<size_t>(padPos_)];
    }

    StageA a_;
    StageB b_;
    StageC c_;
    std::array<float, kMaxFactor + 1> pad_buf_{};
    int factor_ = 4, pad_ = 0, padPos_ = 0, latency_ = 0;
};

} // namespace lampa
