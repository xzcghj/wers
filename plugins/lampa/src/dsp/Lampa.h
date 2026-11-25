#pragma once
// WERS Lampa: a leveling amplifier with an optical or a tube gain cell, a tube make-up stage and
// transformers. Plain C++, no allocation after prepare(), every control smoothed.
#include "Cell.h"
#include "Filters.h"
#include "Oversampler.h"
#include "Tube.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace lampa {

enum class Character { Opto = 0, Tube = 1 };

struct Params {
    float squeeze = 30.0f;     // Ściskanie, 0..100: how far down the threshold goes
    float gainDb = 3.0f;       // Wzmocnienie, the make-up gain into the tube
    float color = 0.25f;       // Kolor, 0..1
    float mix = 1.0f;          // 0 dry .. 1 processed
    Character character = Character::Opto;
    bool limit = false;
    float link = 1.0f;         // stereo link, 0..1
    float sidechainHz = 20.0f; // high-pass of the detector; 20 Hz and below is off
    float outputGain = 1.0f;
    bool bypass = false;
};

// 0 VU on the meter is this many dBFS (RMS of a sine).
inline constexpr float kVuReferenceDb = -18.0f;

// The threshold that Ściskanie sets: +6 dBFS at 0 (nothing happens), -11 dBFS at 30, -50 dBFS at 100.
inline float thresholdDb(float squeeze) { return 6.0f - 0.56f * std::clamp(squeeze, 0.0f, 100.0f); }

class Engine {
public:
    static constexpr int kChunk = 16;
    static constexpr int kMaxLatency = 64;

    Engine() { prepare(48000.0); }

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        // 8x below 60 kHz, 4x up to 120 kHz, 2x above: the tube's harmonics stay far from the band.
        const int factor = fs_ < 60000.0 ? 8 : (fs_ < 120000.0 ? 4 : 2);
        for (auto& o : os_) o.setFactor(factor);
        latency_ = os_[0].latency();
        osFs_ = fs_ * factor;
        sThr_ = onePole(fs_, 0.040);
        sGain_ = onePole(fs_, 0.030);
        sSlow_ = onePole(fs_, 0.060);
        sMode_ = onePole(fs_, 0.080);
        bypassStep_ = static_cast<float>(1.0 / (0.020 * fs_));
        reset();
    }

    void reset() {
        for (int c = 0; c < 2; ++c) {
            cell_[c].reset();
            tube_[c].reset();
            os_[c].reset();
            scHp_[c].reset();
            lowHp_[c].reset();
            weight_[c].reset();
            soften_[c].reset();
            ms_[c] = 0.0f;
            peak_[c] = 0.0f;
            dry_[c].fill(0.0f);
            gr_[c] = 0.0f;
        }
        dryPos_ = 0;
        snapToTarget();
        updateChunk();
        outMeter_ = inMeter_ = 0.0f;
        storeMeters();
    }

    void setParams(const Params& p) {
        target_ = p;
        if (first_) {
            first_ = false;
            snapToTarget();
        }
    }

    int latency() const { return latency_; }
    double sampleRate() const { return fs_; }

    // Stereo, in place. Any length.
    void process(float* left, float* right, int n) {
        float* ch[2] = { left, right };
        for (int i = 0; i < n; ++i) {
            if (chunkLeft_ == 0) {
                updateChunk();
                chunkLeft_ = kChunk;
            }
            --chunkLeft_;
            smoothSample();
            const float x[2] = { ch[0][i], ch[1][i] };

            // The dry signal waits as long as the oversampler, so Mix and bypass line up.
            float dry[2];
            for (int c = 0; c < 2; ++c) {
                dry_[c][static_cast<size_t>(dryPos_)] = x[c];
                dry[c] = dry_[c][static_cast<size_t>((dryPos_ + kDryLen - latency_) % kDryLen)];
            }
            dryPos_ = (dryPos_ + 1) % kDryLen;

            // Detector: high-passed, averaged power, scaled so a sine reads its peak.
            float power[2];
            for (int c = 0; c < 2; ++c) {
                const float s = scOn_ ? scHp_[c].process(x[c], scCoefs_) : x[c];
                ms_[c] += (s * s - ms_[c]) * detCoef_;
                const float a = std::fabs(s);
                peak_[c] = a > peak_[c] ? a : peak_[c] * peakRelease_;
                const float pk = limitNow_ * 0.9f * peak_[c];
                power[c] = std::max(2.0f * ms_[c], pk * pk);
            }
            const float loudest = std::max(power[0], power[1]);
            for (int c = 0; c < 2; ++c) {
                const float p = power[c] + linkNow_ * (loudest - power[c]);
                const float level = 10.0f * std::log10(p + 1e-12f);
                gr_[c] = cell_[c].step(curve_.reduction(level - thrNow_), cellCoefs_);
            }

            float out[2];
            for (int c = 0; c < 2; ++c) {
                const float gain = std::exp(-0.115129255f * gr_[c]) * makeupNow_;
                const float drive = 1.0f + shape_.squeezeDrive * gr_[c];
                TubeStage& tube = tube_[c];
                const TubeShape& shape = shape_;
                float y = os_[c].process(x[c] * gain, [&](float v) { return tube.process(v, shape, drive); });
                y = lowHp_[c].process(y, lowHpCoefs_);
                y = weight_[c].process(y, weightCoefs_);
                y = soften_[c].process(y, softenCoefs_);
                float o = dry[c] + mixNow_ * (y - dry[c]);
                o *= outNow_;
                o += bypassNow_ * (dry[c] - o);
                out[c] = o;
            }
            ch[0][i] = out[0];
            ch[1][i] = out[1];

            // Meters: rectified average, as a VU meter's coil sees it.
            const float inAbs = 0.5f * (std::fabs(x[0]) + std::fabs(x[1]));
            const float outAbs = 0.5f * (std::fabs(out[0]) + std::fabs(out[1]));
            inMeter_ += (inAbs - inMeter_) * meterCoef_;
            outMeter_ += (outAbs - outMeter_) * meterCoef_;
        }
        storeMeters();
    }

    // For the meters, readable from any thread.
    float meterReductionDb() const { return mGr_.load(std::memory_order_relaxed); }
    float meterMemoryDb() const { return mMemory_.load(std::memory_order_relaxed); }
    // Rectified average of the output and input, scaled so a sine reads its RMS.
    float meterOutput() const { return mOut_.load(std::memory_order_relaxed); }
    float meterInput() const { return mIn_.load(std::memory_order_relaxed); }
    // How hard the tube is driven: about 0 clean, 1 where it bends.
    float meterHeat() const { return mHeat_.load(std::memory_order_relaxed); }

    // The reduction of one channel right now, for tests.
    float reductionDb(int channel) const { return gr_[channel & 1]; }
    float memoryDb(int channel) const { return cell_[channel & 1].slow; }

private:
    static constexpr int kDryLen = kMaxLatency + 1;

    void snapToTarget() {
        thrNow_ = thresholdDb(target_.squeeze);
        makeupNow_ = std::pow(10.0f, target_.gainDb / 20.0f);
        mixNow_ = std::clamp(target_.mix, 0.0f, 1.0f);
        outNow_ = target_.outputGain;
        colorNow_ = std::clamp(target_.color, 0.0f, 1.0f);
        tubeNow_ = target_.character == Character::Tube ? 1.0f : 0.0f;
        limitNow_ = target_.limit ? 1.0f : 0.0f;
        linkNow_ = std::clamp(target_.link, 0.0f, 1.0f);
        scHzNow_ = target_.sidechainHz;
        bypassNow_ = target_.bypass ? 1.0f : 0.0f;
    }

    void smoothSample() {
        thrNow_ += (thresholdDb(target_.squeeze) - thrNow_) * sThr_;
        makeupNow_ += (makeupTarget_ - makeupNow_) * sGain_;
        mixNow_ += (std::clamp(target_.mix, 0.0f, 1.0f) - mixNow_) * sGain_;
        outNow_ += (target_.outputGain - outNow_) * sGain_;
        const float b = target_.bypass ? 1.0f : 0.0f;
        bypassNow_ = b > bypassNow_ ? std::min(b, bypassNow_ + bypassStep_) : std::max(b, bypassNow_ - bypassStep_);
    }

    // What changes slowly is worked out once per chunk: filters, curve, timing and the tube's shape.
    void updateChunk() {
        const float k = static_cast<float>(kChunk);
        makeupTarget_ = std::pow(10.0f, std::clamp(target_.gainDb, -40.0f, 40.0f) / 20.0f);
        auto follow = [k](float& now, float to, float coef) { now += (to - now) * (1.0f - std::pow(1.0f - coef, k)); };
        follow(colorNow_, std::clamp(target_.color, 0.0f, 1.0f), sSlow_);
        follow(tubeNow_, target_.character == Character::Tube ? 1.0f : 0.0f, sMode_);
        follow(limitNow_, target_.limit ? 1.0f : 0.0f, sSlow_);
        follow(linkNow_, std::clamp(target_.link, 0.0f, 1.0f), sSlow_);
        follow(scHzNow_, std::clamp(target_.sidechainHz, 20.0f, 1000.0f), sSlow_);

        curve_ = Curve::mix(Curve::mix(kOptoCurve, kOptoLimit, limitNow_), Curve::mix(kTubeCurve, kTubeLimit, limitNow_), tubeNow_);
        auto timing = Timing::mix(kOptoTiming, kTubeTiming, tubeNow_);
        timing.attack *= 1.0f - 0.4f * limitNow_;
        cellCoefs_ = CellCoefs::from(timing, fs_);
        detCoef_ = onePole(fs_, timing.detector);
        peakRelease_ = static_cast<float>(std::exp(-1.0 / (0.030 * fs_)));
        meterCoef_ = onePole(fs_, 0.010);

        scOn_ = scHzNow_ > 20.5f;
        if (scOn_) scCoefs_ = BiquadCoefs::highpass(fs_, scHzNow_, 0.7071);
        shape_ = TubeShape::make(colorNow_, tubeNow_, osFs_);
        // Transformers: a subsonic roll-off, a little weight down low and a softer top, more with Kolor.
        lowHpCoefs_ = BiquadCoefs::highpass(fs_, 10.0, 0.7071);
        weightCoefs_ = BiquadCoefs::peak(fs_, 70.0 - 10.0 * tubeNow_, 0.8, 0.25 + 1.0 * colorNow_);
        softenCoefs_ = BiquadCoefs::highShelf(fs_, 10000.0, 0.6, -(0.2 + 1.6 * colorNow_));
    }

    void storeMeters() {
        const float gr = std::max(gr_[0], gr_[1]);
        mGr_.store(gr, std::memory_order_relaxed);
        mMemory_.store(std::max(cell_[0].slow, cell_[1].slow), std::memory_order_relaxed);
        constexpr float kSineAverageToRms = 1.1107207f; // pi / (2 sqrt 2)
        mOut_.store(outMeter_ * kSineAverageToRms, std::memory_order_relaxed);
        mIn_.store(inMeter_ * kSineAverageToRms, std::memory_order_relaxed);
        mHeat_.store(std::max(tube_[0].heat, tube_[1].heat), std::memory_order_relaxed);
    }

    double fs_ = 48000.0, osFs_ = 192000.0;
    int latency_ = 0;
    Params target_;
    bool first_ = true;
    int chunkLeft_ = 0;

    float sThr_ = 0, sGain_ = 0, sSlow_ = 0, sMode_ = 0, bypassStep_ = 0;
    float thrNow_ = 0, makeupNow_ = 1, makeupTarget_ = 1, mixNow_ = 1, outNow_ = 1, colorNow_ = 0, tubeNow_ = 0, limitNow_ = 0,
          linkNow_ = 1, scHzNow_ = 20, bypassNow_ = 0;

    Curve curve_;
    CellCoefs cellCoefs_;
    float detCoef_ = 0, peakRelease_ = 0, meterCoef_ = 0;
    bool scOn_ = false;
    BiquadCoefs scCoefs_, lowHpCoefs_, weightCoefs_, softenCoefs_;
    TubeShape shape_;

    std::array<Cell, 2> cell_{};
    std::array<TubeStage, 2> tube_{};
    std::array<Oversampler, 2> os_{};
    std::array<Biquad, 2> scHp_{}, lowHp_{}, weight_{}, soften_{};
    std::array<float, 2> ms_{}, peak_{}, gr_{};
    std::array<std::array<float, kDryLen>, 2> dry_{};
    int dryPos_ = 0;
    float outMeter_ = 0, inMeter_ = 0;

    std::atomic<float> mGr_{ 0 }, mMemory_{ 0 }, mOut_{ 0 }, mIn_{ 0 }, mHeat_{ 0 };
};

} // namespace lampa
