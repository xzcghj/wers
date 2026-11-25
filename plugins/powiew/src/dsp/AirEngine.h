#pragma once
// WERS Powiew: a dynamic air enhancer. Two bands, Obecność (presence, around 3 to 10 kHz) and Powietrze (air,
// a shelf from 8 kHz up), each made of two parts that rise together with the knob:
//   - a dynamic EQ: a bell or a shelf mixed in parallel with the dry signal, so at zero it is exactly the dry;
//   - an exciter: the band below is filtered out, run through a Chebyshev waveshaper at 4x the rate (so nothing
//     aliases) and only the new harmonics above it are added.
// A detector that looks a little ahead pulls both parts back when the band is already loud next to the body
// of the sound (sibilance, cymbals, bright mixes) and lets them jump a little on attacks.
// Plain C++ without JUCE; nothing allocates after prepare().
#include "Filters.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace powiew {

enum class Character { Silk = 0, Shine = 1, Analog = 2 };

struct Params {
    float presence = 0.0f, air = 0.0f; // 0..1
    float presenceHz = 6000.0f, airHz = 12000.0f;
    float smooth = 0.5f; // 0..1: how hard the boost backs off on bright, sibilant moments
    Character character = Character::Silk;
    float mix = 1.0f;
    float outputGain = 1.0f;
    bool monoSafe = false; // add only to the mid: identical in both channels, nothing to the sides
    bool delta = false;    // hear only what is added
    bool bypass = false;
};

// What a character sets. Values glide when the character changes, so switching never clicks.
struct Shape {
    float presenceDb, airDb; // the most the dynamic EQ adds at 100%, before attacks
    float harmonics;         // exciter level at 100%
    float h2, h3, h4, h5;    // Chebyshev weights: which harmonics the exciter makes
    float transient;         // how much the boost jumps on attacks (0.5 = up to half as much again)
    float releaseMs;         // how fast the boost comes back after backing off
};

inline constexpr std::array<Shape, 3> kShapes{ {
    { 6.0f, 9.0f, 0.38f, 0.62f, 0.22f, 0.10f, 0.00f, 0.25f, 70.0f }, // Jedwab: soft, mostly the 2nd harmonic
    { 8.0f, 10.0f, 0.48f, 0.25f, 0.55f, 0.05f, 0.15f, 0.50f, 45.0f },  // Blask: brighter, odd harmonics, snappy
    { 5.0f, 7.0f, 0.85f, 0.55f, 0.40f, 0.25f, 0.15f, 0.35f, 60.0f },  // Analog: more harmonics, less EQ
} };

// The fixed boosts the dynamic EQ mixes in: c = 1 is this much, c = 0 is nothing.
inline constexpr double kRefDb = 12.0;
inline const double kRefA = std::pow(10.0, kRefDb / 40.0);
inline const double kRefGain = kRefA * kRefA; // 12 dB as a gain
inline constexpr double kBellQ = 0.8, kShelfQ = 0.58; // a shelf this gentle never dips below the dry level

// The mix amount that gives a boost of dB at the top of the shelf or the centre of the bell.
inline double amountForDb(double db) { return (std::pow(10.0, db / 20.0) - 1.0) / (kRefGain - 1.0); }

// What the dynamic EQ does at hz right now, in dB. For drawing the curve.
inline double boostDbAt(double hz, double fs, double presenceHz, double presenceAmount, double airHz, double airAmount) {
    const double edge = 0.45 * fs;
    const double t = std::tan(kPi * std::min(hz, 0.499 * fs) / fs);
    const double wp = t / std::tan(kPi * std::min(presenceHz, edge) / fs);
    const double wa = t / std::tan(kPi * std::min(airHz, edge) / fs);
    // The two are mixed in parallel with the dry signal, so their differences add.
    const double g = std::abs(1.0 + presenceAmount * bellDiff(wp, kRefGain, kBellQ * kRefA) + airAmount * shelfDiff(wa, kRefA, kShelfQ));
    return 20.0 * std::log10(std::max(1e-9, g));
}

// One band of harmonics for one channel: the source band, the waveshaper at the higher rate and the band of
// new harmonics it keeps.
class Exciter {
public:
    void prepare(double fs, int factor, int extraDelay) {
        fs_ = fs;
        os_.prepare(factor);
        pad_.prepare(extraDelay);
        // The envelope releases over 15 ms, counted at the higher rate.
        release_ = static_cast<float>(std::exp(-1.0 / (0.015 * fs * os_.factor())));
        glide_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * fs * os_.factor())));
        reset();
    }
    void reset() noexcept {
        srcHp_.reset();
        srcLp_.reset();
        postHp1_.reset();
        postHp2_.reset();
        postLp_.reset();
        os_.reset();
        pad_.reset();
        env_ = level_ = 0.0f;
    }
    int latency() const noexcept { return os_.latency() + pad_.samples(); }
    // Source band from lowHz to highHz, the harmonics kept from keepHz up (and below topHz unless it is 0).
    void setBand(double lowHz, double highHz, double keepHz, double topHz) noexcept {
        srcHp_.set(lowHz, 0.7071, fs_);
        srcLp_.set(highHz, 0.7071, fs_);
        postHp1_.set(keepHz, 0.5412, fs_); // two stages: a 4th order Butterworth high-pass
        postHp2_.set(keepHz, 1.3066, fs_);
        useTop_ = topHz > 0.0;
        if (useTop_) postLp_.set(topHz, 0.7071, fs_);
    }
    // weights: h2..h5. Returns the new harmonics, delayed like the dry signal.
    inline float process(float x, const float* weights) noexcept {
        const float source = srcLp_.lowpass(srcHp_.highpass(x));
        float hi[4];
        os_.up(source, hi);
        const int n = os_.factor();
        for (int j = 0; j < n; ++j) {
            const float u = hi[j];
            // A peak follower, then smoothed over 1 ms: the raw peaks of a sampled wave jitter with the sample
            // grid, and that jitter would turn into tones that belong to no harmonic.
            const float a = std::abs(u);
            env_ = a > env_ ? a : env_ * release_;
            level_ += glide_ * (env_ - level_);
            if (level_ < 1e-20f) env_ = level_ = 0.0f;
            // Normalised by its own level, the harmonics keep the same proportion at any loudness. Where an attack
            // outruns the smoothed level, the wave itself is the level, so u / scale never leaves -1..1.
            const float scale = std::max(1.02f * level_, a) + 1e-12f;
            const float x1 = u / scale, x2 = x1 * x1;
            const float t2 = 2.0f * x2 - 1.0f;
            const float t3 = x1 * (4.0f * x2 - 3.0f);
            const float t4 = 8.0f * x2 * (x2 - 1.0f) + 1.0f;
            const float t5 = x1 * (16.0f * x2 * x2 - 20.0f * x2 + 5.0f);
            hi[j] = scale * (weights[0] * t2 + weights[1] * t3 + weights[2] * t4 + weights[3] * t5);
        }
        float h = os_.down(hi);
        // The constant terms of T2 and T4 leave only the slow envelope down low; the high-pass takes it out.
        h = postHp2_.highpass(postHp1_.highpass(h));
        if (useTop_) h = postLp_.lowpass(h);
        return pad_.process(h);
    }
private:
    Svf srcHp_, srcLp_, postHp1_, postHp2_, postLp_;
    Oversampler os_;
    Delay pad_;
    double fs_ = 48000.0;
    float env_ = 0.0f, level_ = 0.0f, release_ = 0.999f, glide_ = 0.01f;
    bool useTop_ = false;
};

class AirEngine {
public:
    static constexpr int kRing = 8192;
    static constexpr int kUpdateEvery = 16; // filter corners follow their knobs this often

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        // 4x below 50 kHz, 2x up to 100 kHz, above that the rate itself leaves room for the harmonics.
        const int factor = fs_ < 50000.0 ? 4 : (fs_ < 100000.0 ? 2 : 1);
        Oversampler probe;
        probe.prepare(factor);
        // At least 1,5 ms, so the detector sees sibilance coming before it reaches the output.
        latency_ = std::max(probe.latency(), static_cast<int>(std::lround(0.0015 * fs_)));
        for (auto& e : exciters_) e.prepare(fs_, factor, latency_ - probe.latency());
        for (auto& d : dry_) d.prepare(latency_);

        const auto perSample = [&](double ms) { return static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * ms * fs_))); };
        smoothCoef_ = perSample(20.0);
        freqCoef_ = perSample(40.0);
        rampStep_ = static_cast<float>(1.0 / (0.02 * fs_));
        // Band and body follow with the same timing, so their ratio is the spectral balance, not the crest factor.
        bodyAttack_ = bandAttack_ = perSample(1.0);
        bodyRelease_ = bandRelease_ = perSample(30.0);
        // Attacks: a 4 ms average of the body against a slow one; the average keeps the waveform's own ripple out.
        fastAttack_ = fastRelease_ = perSample(4.0);
        slowAttack_ = perSample(60.0);
        slowRelease_ = perSample(250.0);
        backoffAttack_ = perSample(0.4);
        meterCoef_ = perSample(120.0);
        for (size_t c = 0; c < 3; ++c) {
            const auto& s = kShapes[c];
            targets_[c] = { static_cast<float>(amountForDb(s.presenceDb)), static_cast<float>(amountForDb(s.airDb)), s.harmonics, s.h2, s.h3, s.h4, s.h5, s.transient,
                            perSample(s.releaseMs) };
        }
        reset();
    }

    void reset() noexcept {
        for (auto& e : exciters_) e.reset();
        for (auto& d : dry_) d.reset();
        for (auto& ch : channels_) ch.reset();
        meterDryP_.reset();
        meterDryP2_.reset();
        meterOutP_.reset();
        meterOutP2_.reset();
        meterDryA_.reset();
        meterOutA_.reset();
        body_ = band_[0] = band_[1] = fast_ = slow_ = 0.0f;
        backoff_[0] = backoff_[1] = 1.0f;
        meterE_ = {};
        for (auto& v : ringOut_) v.store(0.0f, std::memory_order_relaxed);
        for (auto& v : ringAdd_) v.store(0.0f, std::memory_order_relaxed);
        ringWrite_.store(0, std::memory_order_relaxed);
        snap_ = true;
        untilUpdate_ = 0;
        lastP_ = lastA_ = -1.0;
    }

    void setParams(const Params& p) noexcept { target_ = p; }
    int latency() const noexcept { return latency_; }
    double sampleRate() const noexcept { return fs_; }

    void process(float* left, float* right, int n) noexcept {
        if (n <= 0) return;
        const Params& t = target_;
        const auto& shape = targets_[static_cast<size_t>(std::clamp(static_cast<int>(t.character), 0, 2))];
        const float tPresence = std::clamp(t.presence, 0.0f, 1.0f), tAir = std::clamp(t.air, 0.0f, 1.0f);
        const float tMix = std::clamp(t.mix, 0.0f, 1.0f), tGain = std::clamp(t.outputGain, 0.0f, 16.0f);
        const float tSmooth = std::clamp(t.smooth, 0.0f, 1.0f);
        const float tLogP = std::log2(std::clamp(t.presenceHz, 1000.0f, 24000.0f));
        const float tLogA = std::log2(std::clamp(t.airHz, 2000.0f, 30000.0f));
        if (snap_) {
            snap_ = false;
            presence_ = tPresence;
            air_ = tAir;
            mix_ = tMix;
            gain_ = tGain;
            smooth_ = tSmooth;
            logP_ = tLogP;
            logA_ = tLogA;
            shape_ = shape;
            mono_ = t.monoSafe ? 1.0f : 0.0f;
            delta_ = t.delta ? 1.0f : 0.0f;
            bypass_ = t.bypass ? 1.0f : 0.0f;
            updateCorners();
        }
        float maxBackoffP = 0.0f, maxBackoffA = 0.0f;
        int w = ringWrite_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            // Smoothing: every knob glides, the switches cross-fade.
            presence_ += smoothCoef_ * (tPresence - presence_);
            air_ += smoothCoef_ * (tAir - air_);
            mix_ += smoothCoef_ * (tMix - mix_);
            gain_ += smoothCoef_ * (tGain - gain_);
            smooth_ += smoothCoef_ * (tSmooth - smooth_);
            logP_ += freqCoef_ * (tLogP - logP_);
            logA_ += freqCoef_ * (tLogA - logA_);
            shape_.glide(shape, smoothCoef_);
            mono_ = ramp(mono_, t.monoSafe ? 1.0f : 0.0f);
            delta_ = ramp(delta_, t.delta ? 1.0f : 0.0f);
            bypass_ = ramp(bypass_, t.bypass ? 1.0f : 0.0f);
            if (--untilUpdate_ <= 0) updateCorners();

            const float xl = left[i], xr = right[i];

            // --- Detector: looks at the input now, acts on the output a latency later. ---
            float eBody = 0.0f, eP = 0.0f, eA = 0.0f;
            for (int c = 0; c < 2; ++c) {
                auto& ch = channels_[static_cast<size_t>(c)];
                const float x = c == 0 ? xl : xr;
                const float b = ch.bodyLp.lowpass(ch.bodyHp.highpass(x));
                const float p = ch.presLp.lowpass(ch.presHp.highpass(x));
                const float a = ch.airHp2.highpass(ch.airHp.highpass(x));
                eBody += 0.5f * b * b;
                eP += 0.5f * p * p;
                eA += 0.5f * a * a;
            }
            body_ += (eBody > body_ ? bodyAttack_ : bodyRelease_) * (eBody - body_);
            band_[0] += (eP > band_[0] ? bandAttack_ : bandRelease_) * (eP - band_[0]);
            band_[1] += (eA > band_[1] ? bandAttack_ : bandRelease_) * (eA - band_[1]);
            fast_ += (eBody > fast_ ? fastAttack_ : fastRelease_) * (eBody - fast_);
            slow_ += (fast_ > slow_ ? slowAttack_ : slowRelease_) * (fast_ - slow_);

            // How bright is it next to the body of the sound? Above the threshold the boost backs off, softly:
            // 1 / (1 + (ratio / threshold)^3) in amplitude.
            const float depth = std::min(1.0f, 4.0f * smooth_);
            for (int b = 0; b < 2; ++b) {
                const float ratio = band_[b] / (body_ + 1e-10f) * threshold_[b];
                const float target = 1.0f / (1.0f + ratio * std::sqrt(ratio));
                backoff_[b] += (target < backoff_[b] ? backoffAttack_ : shape_.release) * (target - backoff_[b]);
            }
            const float reduceP = depth * (1.0f - backoff_[0]), reduceA = depth * (1.0f - backoff_[1]);
            maxBackoffP = std::max(maxBackoffP, reduceP);
            maxBackoffA = std::max(maxBackoffA, reduceA);
            // Attacks: the body rising faster than its slow average lifts the boost a little.
            const float rise = std::clamp(std::sqrt(fast_ / (slow_ + 1e-12f)) - 1.15f, 0.0f, 1.0f);
            const float lift = 1.0f + shape_.transient * rise;
            const float dynP = lift * (1.0f - reduceP), dynA = lift * (1.0f - reduceA);
            const float eqP = presence_ * shape_.presence * dynP, eqA = air_ * shape_.air * dynA;
            const float exP = presence_ * shape_.harmonics * dynP, exA = air_ * shape_.harmonics * dynA;
            nowP_ = eqP;
            nowA_ = eqA;

            // --- Harmonics, from left, right and mid, already delayed to line up. ---
            const float xm = 0.5f * (xl + xr);
            const float* weights = &shape_.h2;
            const float hpL = exciters_[0].process(xl, weights), hpR = exciters_[1].process(xr, weights), hpM = exciters_[2].process(xm, weights);
            const float haL = exciters_[3].process(xl, weights), haR = exciters_[4].process(xr, weights), haM = exciters_[5].process(xm, weights);

            // --- Dynamic EQ on the delayed dry signal, mixed in parallel. ---
            const float dl = dry_[0].process(xl), dr = dry_[1].process(xr);
            const float eqL = eqP * channels_[0].bellDiff(dl) + eqA * channels_[0].shelfDiff(dl);
            const float eqR = eqP * channels_[1].bellDiff(dr) + eqA * channels_[1].shelfDiff(dr);

            const float addL = eqL + exP * hpL + exA * haL;
            const float addR = eqR + exP * hpR + exA * haR;
            // Mono-safe: the same addition in both channels, built from the mid alone, so the sides stay as they were.
            const float addM = 0.5f * (eqL + eqR) + exP * hpM + exA * haM;
            const float aL = addL + mono_ * (addM - addL), aR = addR + mono_ * (addM - addR);

            const float outL = gain_ * (dl * (1.0f - delta_) + mix_ * aL);
            const float outR = gain_ * (dr * (1.0f - delta_) + mix_ * aR);
            const float finalL = outL * (1.0f - bypass_) + dl * bypass_, finalR = outR * (1.0f - bypass_) + dr * bypass_;
            left[i] = finalL;
            right[i] = finalR;

            // --- Meters: how much louder each band got, on the mid, before the output gain. ---
            const float dm = 0.5f * (dl + dr), pm = dm + mix_ * 0.5f * (aL + aR);
            const float dP = meterDryP2_.lowpass(meterDryP_.highpass(dm)), oP = meterOutP2_.lowpass(meterOutP_.highpass(pm));
            const float dA = meterDryA_.highpass(dm), oA = meterOutA_.highpass(pm);
            meterE_[0] += meterCoef_ * (dP * dP - meterE_[0]);
            meterE_[1] += meterCoef_ * (oP * oP - meterE_[1]);
            meterE_[2] += meterCoef_ * (dA * dA - meterE_[2]);
            meterE_[3] += meterCoef_ * (oA * oA - meterE_[3]);

            ringOut_[static_cast<size_t>(w)].store(0.5f * (finalL + finalR), std::memory_order_relaxed);
            ringAdd_[static_cast<size_t>(w)].store((1.0f - bypass_) * gain_ * mix_ * 0.5f * (aL + aR), std::memory_order_relaxed);
            w = (w + 1) & (kRing - 1);
        }
        ringWrite_.store(w, std::memory_order_relaxed);
        auto addedDb = [](float dry, float out) {
            if (dry < 1e-11f) return 0.0f; // nothing there to measure
            return 10.0f * std::log10((out + 1e-14f) / (dry + 1e-14f));
        };
        addedP_.store(addedDb(meterE_[0], meterE_[1]), std::memory_order_relaxed);
        addedA_.store(addedDb(meterE_[2], meterE_[3]), std::memory_order_relaxed);
        backoffP_.store(maxBackoffP, std::memory_order_relaxed);
        backoffA_.store(maxBackoffA, std::memory_order_relaxed);
        curveP_.store(nowP_, std::memory_order_relaxed);
        curveA_.store(nowA_, std::memory_order_relaxed);
        setP_.store(presence_ * shape_.presence, std::memory_order_relaxed);
        setA_.store(air_ * shape_.air, std::memory_order_relaxed);
        hzP_.store(std::exp2(logP_), std::memory_order_relaxed);
        hzA_.store(std::exp2(logA_), std::memory_order_relaxed);
    }

    // --- What the editor reads. ---
    // How much louder the presence and air bands came out, in dB, over the last 120 ms or so.
    float presenceAddedDb() const noexcept { return addedP_.load(std::memory_order_relaxed); }
    float airAddedDb() const noexcept { return addedA_.load(std::memory_order_relaxed); }
    // How much of the boost the detector took away in the last block, 0..1.
    float presenceBackoff() const noexcept { return backoffP_.load(std::memory_order_relaxed); }
    float airBackoff() const noexcept { return backoffA_.load(std::memory_order_relaxed); }
    // The EQ mix amounts right now and as set (before the detector), with the corners, for drawing the curve.
    float presenceAmountNow() const noexcept { return curveP_.load(std::memory_order_relaxed); }
    float airAmountNow() const noexcept { return curveA_.load(std::memory_order_relaxed); }
    float presenceAmountSet() const noexcept { return setP_.load(std::memory_order_relaxed); }
    float airAmountSet() const noexcept { return setA_.load(std::memory_order_relaxed); }
    float presenceHzNow() const noexcept { return hzP_.load(std::memory_order_relaxed); }
    float airHzNow() const noexcept { return hzA_.load(std::memory_order_relaxed); }
    // The last n samples of the output mid and of what was added to it, oldest first.
    void copySpectrumSources(float* out, float* added, int n) const noexcept {
        n = std::min(n, kRing);
        const int end = ringWrite_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            const auto at = static_cast<size_t>((end - n + i + kRing) & (kRing - 1));
            out[i] = ringOut_[at].load(std::memory_order_relaxed);
            added[i] = ringAdd_[at].load(std::memory_order_relaxed);
        }
    }

private:
    // The character's values in the form the loop uses, so a change glides like any knob.
    struct Live {
        float presence, air, harmonics, h2, h3, h4, h5, transient, release;
        void glide(const Live& to, float k) noexcept {
            presence += k * (to.presence - presence);
            air += k * (to.air - air);
            harmonics += k * (to.harmonics - harmonics);
            h2 += k * (to.h2 - h2);
            h3 += k * (to.h3 - h3);
            h4 += k * (to.h4 - h4);
            h5 += k * (to.h5 - h5);
            transient += k * (to.transient - transient);
            release += k * (to.release - release);
        }
    };

    struct Channel {
        Svf bodyHp, bodyLp, presHp, presLp, airHp, airHp2, bell, shelf;
        void reset() noexcept {
            for (auto* f : { &bodyHp, &bodyLp, &presHp, &presLp, &airHp, &airHp2, &bell, &shelf }) f->reset();
        }
        // What the bell and the shelf at 12 dB add to the dry signal.
        inline float bellDiff(float x) noexcept {
            bell.tick(x);
            return static_cast<float>(kRefGain - 1.0) * bell.k * bell.v1;
        }
        inline float shelfDiff(float x) noexcept {
            shelf.tick(x);
            const float a = static_cast<float>(kRefA);
            return (a * a - 1.0f) * x + shelf.k * (1.0f - a) * a * shelf.v1 + (1.0f - a * a) * shelf.v2;
        }
    };

    inline float ramp(float v, float to) const noexcept { return to > v ? std::min(to, v + rampStep_) : std::max(to, v - rampStep_); }

    void updateCorners() noexcept {
        untilUpdate_ = kUpdateEvery;
        const double p = std::exp2(static_cast<double>(logP_)), a = std::exp2(static_cast<double>(logA_));
        // The thresholds only change with the smoothness knob; recomputing them here keeps pow out of the loop.
        // Band over body: a vowel sits near -25 dB, a dull mix near -13, white noise near +3, an "s" far above.
        const double thresholdDb = 7.0 - 24.0 * static_cast<double>(smooth_);
        threshold_[0] = static_cast<float>(std::pow(10.0, -thresholdDb / 10.0));
        threshold_[1] = static_cast<float>(std::pow(10.0, -(thresholdDb - 4.0) / 10.0));
        if (std::abs(p - lastP_) < 1e-3 * p && std::abs(a - lastA_) < 1e-3 * a) return;
        lastP_ = p;
        lastA_ = a;
        for (auto& ch : channels_) {
            ch.bodyHp.set(200.0, 0.7071, fs_);
            ch.bodyLp.set(3000.0, 0.7071, fs_);
            ch.presHp.set(0.7 * p, 0.7071, fs_);
            ch.presLp.set(1.6 * p, 0.7071, fs_);
            ch.airHp.set(0.7 * a, 0.5412, fs_);
            ch.airHp2.set(0.7 * a, 1.3066, fs_);
            ch.bell.setG(Svf::warp(p, fs_), static_cast<float>(kBellQ * kRefA));
            ch.shelf.setG(Svf::warp(a, fs_) * static_cast<float>(std::sqrt(kRefA)), static_cast<float>(kShelfQ));
        }
        for (int c = 0; c < 3; ++c) {
            exciters_[static_cast<size_t>(c)].setBand(p / 3.2, p / 1.35, 0.62 * p, 2.4 * p);
            exciters_[static_cast<size_t>(c + 3)].setBand(a / 2.8, a / 1.3, 0.72 * a, 0.0);
        }
        meterDryP_.set(0.65 * p, 0.7071, fs_);
        meterOutP_.set(0.65 * p, 0.7071, fs_);
        meterDryP2_.set(1.7 * p, 0.7071, fs_);
        meterOutP2_.set(1.7 * p, 0.7071, fs_);
        meterDryA_.set(0.75 * a, 0.7071, fs_);
        meterOutA_.set(0.75 * a, 0.7071, fs_);
    }

    double fs_ = 48000.0;
    int latency_ = 0;
    Params target_;
    std::array<Live, 3> targets_{};
    Live shape_{};
    std::array<Exciter, 6> exciters_; // presence L, R, M, then air L, R, M
    std::array<Delay, 2> dry_;
    std::array<Channel, 2> channels_{};
    Svf meterDryP_, meterDryP2_, meterOutP_, meterOutP2_, meterDryA_, meterOutA_;

    float presence_ = 0, air_ = 0, mix_ = 1, gain_ = 1, smooth_ = 0.5f, logP_ = 12, logA_ = 13;
    float mono_ = 0, delta_ = 0, bypass_ = 0;
    float smoothCoef_ = 0, freqCoef_ = 0, rampStep_ = 0;
    float bodyAttack_ = 0, bodyRelease_ = 0, bandAttack_ = 0, bandRelease_ = 0, fastAttack_ = 0, fastRelease_ = 0;
    float slowAttack_ = 0, slowRelease_ = 0, backoffAttack_ = 0, meterCoef_ = 0;
    float body_ = 0, fast_ = 0, slow_ = 0;
    std::array<float, 2> band_{}, backoff_{ 1.0f, 1.0f }, threshold_{ 1.0f, 1.0f };
    std::array<float, 4> meterE_{};
    float nowP_ = 0, nowA_ = 0;
    double lastP_ = -1.0, lastA_ = -1.0;
    bool snap_ = true;
    int untilUpdate_ = 0;

    std::atomic<float> addedP_{ 0.0f }, addedA_{ 0.0f }, backoffP_{ 0.0f }, backoffA_{ 0.0f };
    std::atomic<float> curveP_{ 0.0f }, curveA_{ 0.0f }, setP_{ 0.0f }, setA_{ 0.0f }, hzP_{ 6000.0f }, hzA_{ 12000.0f };
    std::array<std::atomic<float>, kRing> ringOut_{}, ringAdd_{};
    std::atomic<int> ringWrite_{ 0 };
};

} // namespace powiew
