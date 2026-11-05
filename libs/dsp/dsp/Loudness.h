#pragma once
// ITU-R BS.1770-4 / EBU R128 loudness and true peak. All memory is allocated in prepare().
#include "Filters.h"

namespace wers {

class KWeighting {
public:
    void prepare(double fs) noexcept;
    void reset() noexcept { a_.reset(); b_.reset(); }
    double tick(double x) noexcept { return b_.tick(a_.tick(x)); }
private:
    Biquad a_, b_;
};

class LoudnessMeter {
public:
    void prepare(double fs);
    void reset() noexcept;
    // Mono input counts as dual mono, because it plays on both speakers.
    void process(const float* const* ch, int numChannels, int numSamples) noexcept;

    double momentaryLufs() const noexcept { return momentary_; }
    double shortTermLufs() const noexcept { return shortTerm_; }
    double integratedLufs() const noexcept; // -inf-like -140 until enough signal was heard
    double truePeakDb() const noexcept { return gainToDb(truePeak_); }
    double seconds() const noexcept { return static_cast<double>(totalSamples_) / fs_; }
private:
    static constexpr int kHistBins = 2000; // -130 .. +70 LUFS in 0.1 LU steps
    static constexpr int kTpPhases = 4, kTpTaps = 12;
    void closeSubBlock() noexcept;

    double fs_ = 48000.0;
    std::array<KWeighting, 2> k_{};
    int subLen_ = 4800, subCount_ = 0;
    double subEnergy_ = 0.0;
    std::array<double, 30> ring_{};
    int ringPos_ = 0, ringFill_ = 0;
    double momentary_ = -140.0, shortTerm_ = -140.0;
    std::vector<uint32_t> hist_;
    std::array<std::array<float, kTpTaps>, kTpPhases> tpCoef_{};
    std::array<std::array<float, kTpTaps>, 2> tpHist_{};
    int tpPos_ = 0;
    double truePeak_ = 0.0;
    int64_t totalSamples_ = 0;
};

} // namespace wers
