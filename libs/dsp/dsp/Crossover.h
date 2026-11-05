#pragma once
// Splits a signal into up to three Linkwitz-Riley 24 dB/oct bands whose sum has a flat magnitude.
#include "Filters.h"

namespace wers {

class Crossover3 {
public:
    void prepare(double fs);
    void reset() noexcept;
    void setBands(int bands) noexcept;
    // f2 is never below f1. Safe to call while running: the filters stay stable when they move.
    void setFrequencies(float f1, float f2) noexcept;
    // One band: low = x. Two bands: low and high around f1, mid = 0. Three bands: all of them.
    void process(float x, float& low, float& mid, float& high) noexcept;
private:
    double fs_ = 48000.0;
    int bands_ = 1;
    Svf split1_, low1_, high1_; // two second-order filters in a row at f1
    Svf split2_, low2_, high2_; // the same at f2, on what is above f1
    Svf allpass2_;              // gives the low band the phase turn the upper two got at f2
};

} // namespace wers
