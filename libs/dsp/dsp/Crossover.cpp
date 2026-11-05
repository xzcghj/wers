#include "Crossover.h"

namespace wers {

namespace {
constexpr double kButterworthQ = 0.70710678118654752;
}

void Crossover3::prepare(double fs) {
    fs_ = fs;
    setFrequencies(200.0f, 3000.0f);
    reset();
}

void Crossover3::reset() noexcept {
    for (Svf* s : { &split1_, &low1_, &high1_, &split2_, &low2_, &high2_, &allpass2_ }) s->reset();
}

void Crossover3::setBands(int bands) noexcept {
    const int next = std::clamp(bands, 1, 3);
    if (next != bands_) reset();
    bands_ = next;
}

void Crossover3::setFrequencies(float f1, float f2) noexcept {
    const double a = std::max(10.0, static_cast<double>(f1));
    const double b = std::max(a, static_cast<double>(f2));
    for (Svf* s : { &split1_, &low1_, &high1_ }) s->set(a, kButterworthQ, fs_);
    for (Svf* s : { &split2_, &low2_, &high2_, &allpass2_ }) s->set(b, kButterworthQ, fs_);
}

void Crossover3::process(float x, float& low, float& mid, float& high) noexcept {
    if (bands_ == 1) {
        low = x;
        mid = high = 0.0f;
        return;
    }
    const auto first = split1_.tick(static_cast<double>(x));
    const double below = low1_.tick(first.lp).lp;
    const double above = high1_.tick(first.hp).hp;
    if (bands_ == 2) {
        low = static_cast<float>(below);
        mid = 0.0f;
        high = static_cast<float>(above);
        return;
    }
    const auto second = split2_.tick(above);
    mid = static_cast<float>(low2_.tick(second.lp).lp);
    high = static_cast<float>(high2_.tick(second.hp).hp);
    low = static_cast<float>(below - 2.0 * allpass2_.k() * allpass2_.tick(below).bp);
}

} // namespace wers
