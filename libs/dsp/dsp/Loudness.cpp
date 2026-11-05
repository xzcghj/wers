#include "Loudness.h"

namespace wers {

void KWeighting::prepare(double fs) noexcept {
    // Analogue prototype parameters from BS.1770, re-derived for any sample rate.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        a_.setRaw((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
        b_.setRaw(1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
    }
    reset();
}

void LoudnessMeter::prepare(double fs) {
    fs_ = fs;
    subLen_ = std::max(1, static_cast<int>(std::lround(fs * 0.1)));
    for (auto& k : k_) k.prepare(fs);
    hist_.assign(kHistBins, 0u);
    // Windowed-sinc 4x interpolator for true peak, one short FIR per phase.
    for (int p = 0; p < kTpPhases; ++p) {
        double sum = 0.0;
        for (int t = 0; t < kTpTaps; ++t) {
            const double x = t - (5.0 + static_cast<double>(p) / kTpPhases);
            const double s = std::abs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = 0.5 + 0.5 * std::cos(kPi * x / (kTpTaps * 0.5 + 0.5));
            const double v = s * w;
            tpCoef_[static_cast<size_t>(p)][static_cast<size_t>(t)] = static_cast<float>(v);
            sum += v;
        }
        for (auto& v : tpCoef_[static_cast<size_t>(p)]) v = static_cast<float>(v / sum);
    }
    reset();
}

void LoudnessMeter::reset() noexcept {
    for (auto& k : k_) k.reset();
    subCount_ = 0; subEnergy_ = 0.0;
    ring_.fill(0.0); ringPos_ = ringFill_ = 0;
    momentary_ = shortTerm_ = -140.0;
    std::fill(hist_.begin(), hist_.end(), 0u);
    for (auto& h : tpHist_) h.fill(0.0f);
    tpPos_ = 0; truePeak_ = 0.0; totalSamples_ = 0;
}

void LoudnessMeter::process(const float* const* ch, int numChannels, int numSamples) noexcept {
    if (numChannels <= 0 || hist_.empty()) return;
    const int nc = std::min(numChannels, 2);
    for (int i = 0; i < numSamples; ++i) {
        double e = 0.0;
        for (int c = 0; c < nc; ++c) {
            const double y = k_[static_cast<size_t>(c)].tick(ch[c][i]);
            e += y * y;
        }
        if (nc == 1) e *= 2.0;
        subEnergy_ += e;
        if (++subCount_ >= subLen_) closeSubBlock();

        for (int c = 0; c < nc; ++c) {
            auto& h = tpHist_[static_cast<size_t>(c)];
            h[static_cast<size_t>(tpPos_)] = ch[c][i];
            for (int p = 0; p < kTpPhases; ++p) {
                float acc = 0.0f;
                int idx = tpPos_;
                const auto& co = tpCoef_[static_cast<size_t>(p)];
                for (int t = 0; t < kTpTaps; ++t) {
                    acc += co[static_cast<size_t>(t)] * h[static_cast<size_t>(idx)];
                    if (--idx < 0) idx = kTpTaps - 1;
                }
                truePeak_ = std::max(truePeak_, static_cast<double>(std::abs(acc)));
            }
        }
        if (++tpPos_ >= kTpTaps) tpPos_ = 0;
    }
    totalSamples_ += numSamples;
}

void LoudnessMeter::closeSubBlock() noexcept {
    ring_[static_cast<size_t>(ringPos_)] = subEnergy_ / subCount_;
    ringPos_ = (ringPos_ + 1) % static_cast<int>(ring_.size());
    ringFill_ = std::min(ringFill_ + 1, static_cast<int>(ring_.size()));
    subEnergy_ = 0.0; subCount_ = 0;

    const auto meanOf = [this](int blocks) {
        double s = 0.0;
        int idx = ringPos_;
        for (int i = 0; i < blocks; ++i) {
            idx = idx == 0 ? static_cast<int>(ring_.size()) - 1 : idx - 1;
            s += ring_[static_cast<size_t>(idx)];
        }
        return s / blocks;
    };
    if (ringFill_ >= 4) {
        const double m = meanOf(4);
        momentary_ = m > 1e-14 ? -0.691 + 10.0 * std::log10(m) : -140.0;
        if (momentary_ > -70.0) {
            const int bin = std::clamp(static_cast<int>((momentary_ + 130.0) * 10.0), 0, kHistBins - 1);
            ++hist_[static_cast<size_t>(bin)];
        }
    }
    if (ringFill_ >= 30) {
        const double s = meanOf(30);
        shortTerm_ = s > 1e-14 ? -0.691 + 10.0 * std::log10(s) : -140.0;
    } else if (ringFill_ >= 4) {
        const double s = meanOf(ringFill_);
        shortTerm_ = s > 1e-14 ? -0.691 + 10.0 * std::log10(s) : -140.0;
    }
}

double LoudnessMeter::integratedLufs() const noexcept {
    if (hist_.empty()) return -140.0;
    const auto binEnergy = [](int b) { return std::pow(10.0, (((b + 0.5) * 0.1 - 130.0) + 0.691) / 10.0); };
    double e = 0.0; uint64_t n = 0;
    for (int b = 0; b < kHistBins; ++b) {
        const uint32_t c = hist_[static_cast<size_t>(b)];
        if (c) { e += c * binEnergy(b); n += c; }
    }
    if (n == 0) return -140.0;
    const double gate = -0.691 + 10.0 * std::log10(e / static_cast<double>(n)) - 10.0;
    e = 0.0; n = 0;
    for (int b = 0; b < kHistBins; ++b) {
        if ((b + 0.5) * 0.1 - 130.0 < gate) continue;
        const uint32_t c = hist_[static_cast<size_t>(b)];
        if (c) { e += c * binEnergy(b); n += c; }
    }
    if (n == 0) return -140.0;
    return -0.691 + 10.0 * std::log10(e / static_cast<double>(n));
}

} // namespace wers
