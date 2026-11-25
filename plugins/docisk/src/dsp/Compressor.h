// WERS Docisk: the compressor itself, in plain C++ so it can be tested without JUCE.
//
// Signal flow, per sample:
//   key (the main input, or the external sidechain) -> high-pass -> tilt -> level (peak/RMS blend)
//   -> stereo link -> three detector designs side by side (only the chosen one is heard, the others
//   keep running so a switch can crossfade) -> program-dependent release -> gain
//   main input -> lookahead delay -> gain and makeup -> dry/wet mix -> output gain -> soft clip
// Nothing allocates after prepare() and every control is smoothed.
#pragma once
#include "Oversampler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace doc {

inline constexpr float kRatioInfinite = 100.0f;  // a ratio at or above this is infinity
inline constexpr float kRatioMin = 0.5f;         // below 1:1 the curve expands upwards
inline constexpr float kRangeUnlimited = 60.0f;  // a range at or above this does not limit
inline constexpr float kMaxLookaheadMs = 10.0f;
inline constexpr float kNoLimitDb = 120.0f;

enum class Character : int { Clean = 0, Punch = 1, Glue = 2 };

struct Params {
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float kneeDb = 6.0f;
    float attackMs = 10.0f;
    float releaseMs = 120.0f;
    bool autoRelease = false;
    float rangeDb = kRangeUnlimited;
    float makeupDb = 0.0f;
    bool autoMakeup = false;
    float mix = 1.0f;       // 0 dry .. 1 compressed
    float outputDb = 0.0f;
    float rms = 0.0f;       // detector: 0 peak .. 1 RMS
    Character character = Character::Clean;
    float link = 1.0f;      // 0 each channel on its own .. 1 both from the louder one
    float lookaheadMs = 0.0f;
    float keyHighPassHz = 20.0f;
    float keyTiltDb = 0.0f;  // + makes the detector hear more highs
    bool external = false;
    bool listen = false;
    bool clip = false;
};

// The slope of the curve above the knee, in dB of gain per dB of level: 0 at 1:1, -1 at infinity, +1 at 1:0,5.
inline float slopeForRatio(float ratio) noexcept { return ratio >= kRatioInfinite ? -1.0f : 1.0f / std::max(kRatioMin, ratio) - 1.0f; }
inline float rangeLimitDb(float range) noexcept { return range >= kRangeUnlimited - 0.001f ? kNoLimitDb : std::max(0.0f, range); }

// The static curve: the gain in dB that a steady level gets. The knee is a parabola of width `knee` dB centred
// on the threshold, and `range` is the most the gain may move either way.
inline float curveGainDb(float level, float threshold, float slope, float knee, float range) noexcept {
    const float over = level - threshold;
    float g = 0.0f;
    if (knee > 1e-3f && std::abs(over) < 0.5f * knee) {
        const float u = over + 0.5f * knee;
        g = slope * u * u / (2.0f * knee);
    } else if (over > 0.0f) {
        g = slope * over;
    }
    return std::clamp(g, -range, range);
}

// Automatic makeup gives back half of what a loud signal, peaking at -10 dBFS, loses. On real material that lands
// close to the loudness it came in with: the loud parts a little lower, the quiet ones, which the curve leaves
// alone, a little higher.
inline constexpr float kAutoMakeupReferenceDb = -10.0f;
inline float autoMakeupDb(float threshold, float slope, float knee, float range) noexcept {
    return -0.5f * curveGainDb(kAutoMakeupReferenceDb, threshold, slope, knee, range);
}

// The feedback detector hears the level after the gain. This is the gain it has to ask for at that level so that,
// once settled, the result lies on the very same static curve as feed-forward. `derivative` is d gain / d heard.
inline float feedbackGainDb(float heard, float threshold, float slope, float knee, float range, float& derivative) noexcept {
    const float over = heard - threshold;
    const float kneeTop = 0.5f * knee * (1.0f + slope); // where the knee ends, measured after the gain
    float h = 0.0f, d = 0.0f;
    if (knee > 1e-3f && over > -0.5f * knee && over < kneeTop) {
        // Inverts out = in + c (in - T + W/2)^2 for the input, in the stable form of the quadratic root.
        const float v = over + 0.5f * knee, c = slope / (2.0f * knee);
        const float u = 2.0f * v / (1.0f + std::sqrt(std::max(0.0f, 1.0f + 4.0f * c * v)));
        h = v - u;
        d = 1.0f - 1.0f / (1.0f + 2.0f * c * u);
    } else if (over > 0.0f && over >= kneeTop) {
        d = slope / (1.0f + slope);
        h = over * d;
    }
    if (h < -range) { h = -range; d = 0.0f; }
    if (h > range) { h = range; d = 0.0f; }
    derivative = d;
    return h;
}

// One-pole coefficient for a time constant in milliseconds.
inline float poleFor(double ms, double fs) noexcept { return static_cast<float>(std::exp(-1000.0 / (std::max(1e-3, ms) * fs))); }

inline float dbToGain(float db) noexcept { return std::pow(10.0f, 0.05f * db); }
inline float gainToDb(float g) noexcept { return 20.0f * std::log10(std::max(g, 1e-9f)); }

// A value that glides towards its target with a one-pole curve.
struct Glide {
    float value = 0.0f, target = 0.0f, k = 0.0f;
    void setTime(double ms, double fs) noexcept { k = 1.0f - poleFor(ms, fs); }
    void snap(float v) noexcept { value = target = v; }
    float next() noexcept {
        value += k * (target - value);
        return value;
    }
};

// Second-order Butterworth high-pass as a state-variable filter, which stays clean while its frequency moves.
struct KeyHighPass {
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, g = 0.0f;
    static constexpr float k = 1.41421356f;
    void set(float hz, double fs) noexcept {
        g = static_cast<float>(std::tan(3.14159265358979 * std::min<double>(hz, 0.45 * fs) / fs));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    float process(float v0) noexcept {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v0 - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

// What the editor reads: meters that hold their peak until read, and a history of short frames.
struct Frame {
    float in = 0.0f, out = 0.0f, gain = 0.0f, level = -120.0f;
};

class History {
public:
    static constexpr uint32_t kSize = 4096;
    void push(const Frame& f) noexcept {
        const uint32_t at = written_.load(std::memory_order_relaxed);
        auto& slot = slots_[at % kSize];
        slot[0].store(f.in, std::memory_order_relaxed);
        slot[1].store(f.out, std::memory_order_relaxed);
        slot[2].store(f.gain, std::memory_order_relaxed);
        slot[3].store(f.level, std::memory_order_relaxed);
        written_.store(at + 1, std::memory_order_release);
    }
    // Copies the frames written since `cursor` (at most `max`, the newest ones) and moves the cursor on.
    int read(uint32_t& cursor, Frame* out, int max) const noexcept {
        const uint32_t end = written_.load(std::memory_order_acquire);
        uint32_t from = cursor;
        if (end - from > static_cast<uint32_t>(max)) from = end - static_cast<uint32_t>(max);
        if (end - from > kSize / 2) from = end - kSize / 2;
        int n = 0;
        for (uint32_t i = from; i != end; ++i, ++n) {
            const auto& slot = slots_[i % kSize];
            out[n] = { slot[0].load(std::memory_order_relaxed), slot[1].load(std::memory_order_relaxed),
                       slot[2].load(std::memory_order_relaxed), slot[3].load(std::memory_order_relaxed) };
        }
        cursor = end;
        return n;
    }
    uint32_t written() const noexcept { return written_.load(std::memory_order_acquire); }
private:
    std::array<std::array<std::atomic<float>, 4>, kSize> slots_{};
    std::atomic<uint32_t> written_{ 0 };
};

struct Meters {
    std::array<std::atomic<float>, 2> inPeak{}, outPeak{};  // linear, held until taken
    std::atomic<float> gainDb{ 0.0f };                      // the gain right now, both channels' deeper one
    std::atomic<float> gainHoldDb{ 0.0f };                  // the deepest since taken
    std::atomic<float> levelDb{ -120.0f };                  // the loudest the detector heard in the last block
    std::atomic<float> clipDb{ 0.0f };                      // how far the soft clip bent the peaks, held until taken
    std::atomic<bool> keyExternal{ false };                 // the detector is hearing the external sidechain

    static void raise(std::atomic<float>& a, float v) noexcept {
        float old = a.load(std::memory_order_relaxed);
        while (v > old && !a.compare_exchange_weak(old, v, std::memory_order_relaxed)) {}
    }
    static void lower(std::atomic<float>& a, float v) noexcept {
        float old = a.load(std::memory_order_relaxed);
        while (v < old && !a.compare_exchange_weak(old, v, std::memory_order_relaxed)) {}
    }
    static float take(std::atomic<float>& a, float reset = 0.0f) noexcept { return a.exchange(reset, std::memory_order_relaxed); }
};

class Compressor {
public:
    static constexpr int kSub = 16; // envelope times and filter settings move on this grid

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int maxDelay = static_cast<int>(std::ceil(kMaxLookaheadMs * 0.001 * fs_)) + 2;
        int size = 1;
        while (size < maxDelay + 2) size <<= 1;
        for (auto& line : lines_) line.assign(static_cast<size_t>(size), 0.0f);
        mask_ = size - 1;
        frameLength_ = std::max(1, static_cast<int>(std::lround(fs_ * 0.005)));
        delayFadeLength_ = std::max(1, static_cast<int>(std::lround(fs_ * 0.02)));
        rmsPole_ = poleFor(10.0, fs_);
        buildPole_ = poleFor(500.0, fs_);
        tiltK_ = 1.0f - poleFor(1000.0 / (2.0 * 3.14159265358979 * 1000.0), fs_);
        for (auto* gl : { &threshold_, &slope_, &knee_, &range_, &makeup_, &mix_, &output_, &rms_, &link_, &tilt_ })
            gl->setTime(20.0, fs_);
        for (auto* gl : { &external_, &listen_, &clip_, &auto_ }) gl->setTime(8.0, fs_);
        for (auto& gl : character_) gl.setTime(8.0, fs_);
        for (auto* gl : { &logAttack_, &logRelease_, &logHighPass_ }) gl->setTime(30.0, fs_ / kSub);
        for (auto& o : oversampler_) o.prepare();
        reset();
    }

    void reset() noexcept {
        for (auto& line : lines_) std::fill(line.begin(), line.end(), 0.0f);
        write_ = 0;
        delayNow_ = delayOld_ = delayTarget_ = targetDelaySamples();
        delayFade_ = 0;
        for (int c = 0; c < 2; ++c) {
            ms_[c] = 0.0f;
            clean_[c] = punch1_[c] = punch2_[c] = glue_[c] = slow_[c] = gain_[c] = 0.0f;
            highPass_[c].reset();
            tiltLow_[c] = 0.0f;
            oversampler_[c].reset();
        }
        frameAt_ = 0;
        frame_ = Frame{};
        snapControls();
        sub_ = 0;
    }

    void setParams(const Params& p) noexcept {
        p_ = p;
        if (!primed_) {
            primed_ = true;
            snapControls();
            delayNow_ = delayOld_ = delayTarget_ = targetDelaySamples();
        }
        setTargets();
        const int wanted = targetDelaySamples();
        if (wanted != delayTarget_) delayTarget_ = wanted;
    }

    // The latency the host has to compensate: the lookahead, in samples, once any change has faded in.
    int latencySamples() const noexcept { return delayTarget_; }

    // l and r are processed in place. keyL/keyR are the external sidechain (null when there is none).
    // dryL/dryR, when given, receive the input delayed like the output, for a bypass that stays in time.
    void process(float* l, float* r, const float* keyL, const float* keyR, int n, float* dryL = nullptr, float* dryR = nullptr) noexcept {
        if (n <= 0) return;
        const bool haveKey = keyL != nullptr && keyR != nullptr;
        external_.target = (p_.external && haveKey) ? 1.0f : 0.0f;
        if (!haveKey) external_.snap(0.0f); // a key that is gone cannot be faded out of
        meters.keyExternal.store(p_.external && haveKey, std::memory_order_relaxed);
        float inPeak[2] = { 0.0f, 0.0f }, outPeak[2] = { 0.0f, 0.0f }, clipPeak = 0.0f, gainLow = 0.0f, levelHigh = -120.0f;
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i) {
            if (sub_ == 0) updateSlowControls();
            if (++sub_ == kSub) sub_ = 0;

            const float threshold = threshold_.next(), slope = slope_.next(), knee = knee_.next(), range = range_.next();
            const float makeup = makeup_.next(), mix = mix_.next(), outGain = output_.next(), rmsBlend = rms_.next();
            const float link = link_.next(), ext = external_.next(), listen = listen_.next(), clip = clip_.next();
            const float autoRel = auto_.next();
            const float w0 = character_[0].next(), w1 = character_[1].next(), w2 = character_[2].next();

            // The key: what the detector hears, filtered.
            float key[2], level[2];
            for (int c = 0; c < 2; ++c) {
                float k = io[c][i];
                if (haveKey && ext > 1e-6f) k += ext * ((c == 0 ? keyL[i] : keyR[i]) - k);
                k = highPass_[c].process(k);
                tiltLow_[c] += tiltK_ * (k - tiltLow_[c]);
                k = tiltLowGain_ * tiltLow_[c] + tiltHighGain_ * (k - tiltLow_[c]);
                key[c] = k;
                // Peak and RMS read a steady sine alike: the RMS is scaled by root two.
                const float peak = std::abs(k);
                ms_[c] = rmsPole_ * ms_[c] + (1.0f - rmsPole_) * k * k;
                const float rmsLevel = std::sqrt(2.0f * ms_[c]);
                level[c] = gainToDb(peak + rmsBlend * (rmsLevel - peak));
            }
            const float loudest = std::max(level[0], level[1]);

            // Main and key into the lookahead lines, the delayed ones out.
            float delayed[2], keyHeard[2];
            for (int c = 0; c < 2; ++c) {
                lines_[static_cast<size_t>(c)][static_cast<size_t>(write_)] = io[c][i];
                lines_[static_cast<size_t>(c + 2)][static_cast<size_t>(write_)] = key[c];
            }
            readDelayed(delayed, keyHeard);
            write_ = (write_ + 1) & mask_;

            float gainDb[2];
            for (int c = 0; c < 2; ++c) {
                const float heard = level[c] + link * (loudest - level[c]);
                const float target = curveGainDb(heard, threshold, slope, knee, range);
                // Clean: one pole each way, in dB, so the times mean what they say.
                // Attack is any move away from unity gain: down when compressing, up when expanding.
                clean_[c] = away(target, clean_[c]) ? attack_ * clean_[c] + (1.0f - attack_) * target
                                                    : release_ * clean_[c] + (1.0f - release_) * target;
                // Punch: two half-time poles on the attack, so the first milliseconds of a hit get through.
                punch1_[c] = away(target, punch1_[c]) ? attackHalf_ * punch1_[c] + (1.0f - attackHalf_) * target
                                                      : release_ * punch1_[c] + (1.0f - release_) * target;
                punch2_[c] = away(punch1_[c], punch2_[c]) ? attackHalf_ * punch2_[c] + (1.0f - attackHalf_) * punch1_[c] : punch1_[c];
                // Glue: feedback, solved without a delay in the loop.
                glue_[c] = solveFeedback(heard, glue_[c], threshold, std::max(slope, -0.99f), knee, range);
                float g = w0 * clean_[c] + w1 * punch2_[c] + w2 * glue_[c];
                // Program-dependent release: a slow memory of the gain builds up while compression lasts and holds
                // the release back, while short peaks let go fast.
                slow_[c] = g < slow_[c] ? buildPole_ * slow_[c] + (1.0f - buildPole_) * g
                                         : slowRelease_ * slow_[c] + (1.0f - slowRelease_) * g;
                if (g < 0.0f && autoRel > 0.0f) g += autoRel * (std::min(g, slow_[c]) - g);
                gain_[c] = g;
                gainDb[c] = g;
            }

            float outs[2];
            for (int c = 0; c < 2; ++c) {
                const float wet = delayed[c] * dbToGain(gainDb[c] + makeup);
                float y = (delayed[c] + mix * (wet - delayed[c])) * outGain;
                if (clip > 1e-4f) {
                    const float bent = oversampler_[c].process(y, clipPeak);
                    y += clip * (bent - y);
                }
                y += listen * (keyHeard[c] - y);
                outs[c] = y;
                if (dryL != nullptr && dryR != nullptr) (c == 0 ? dryL : dryR)[i] = delayed[c];
                inPeak[c] = std::max(inPeak[c], std::abs(delayed[c]));
                outPeak[c] = std::max(outPeak[c], std::abs(y));
                io[c][i] = y;
            }
            const float deeper = std::abs(gainDb[0]) > std::abs(gainDb[1]) ? gainDb[0] : gainDb[1];
            if (std::abs(deeper) > std::abs(gainLow)) gainLow = deeper;

            // History frame.
            frame_.in = std::max({ frame_.in, std::abs(delayed[0]), std::abs(delayed[1]) });
            frame_.out = std::max({ frame_.out, std::abs(outs[0]), std::abs(outs[1]) });
            if (std::abs(deeper) > std::abs(frame_.gain)) frame_.gain = deeper;
            frame_.level = std::max(frame_.level, loudest);
            if (++frameAt_ >= frameLength_) {
                history.push(frame_);
                frame_ = Frame{};
                frameAt_ = 0;
            }
            levelHigh = std::max(levelHigh, loudest);
            lastGain_ = deeper;
        }
        for (int c = 0; c < 2; ++c) {
            Meters::raise(meters.inPeak[static_cast<size_t>(c)], inPeak[c]);
            Meters::raise(meters.outPeak[static_cast<size_t>(c)], outPeak[c]);
        }
        meters.gainDb.store(lastGain_, std::memory_order_relaxed);
        Meters::lower(meters.gainHoldDb, gainLow);
        meters.levelDb.store(levelHigh, std::memory_order_relaxed);
        if (clipPeak > 1.0f) Meters::raise(meters.clipDb, gainToDb(clipPeak));
    }

    double sampleRate() const noexcept { return fs_; }
    const Params& params() const noexcept { return p_; }
    float currentGainDb(int channel) const noexcept { return gain_[channel & 1]; }

    Meters meters;
    History history;
private:
    int targetDelaySamples() const noexcept {
        const float ms = std::clamp(p_.lookaheadMs, 0.0f, kMaxLookaheadMs);
        return static_cast<int>(std::lround(ms * 0.001 * fs_));
    }

    void setTargets() noexcept {
        const float slope = slopeForRatio(p_.ratio);
        const float range = rangeLimitDb(p_.rangeDb);
        threshold_.target = std::clamp(p_.thresholdDb, -80.0f, 0.0f);
        slope_.target = slope;
        knee_.target = std::clamp(p_.kneeDb, 0.0f, 30.0f);
        range_.target = range;
        float makeup = p_.makeupDb;
        if (p_.autoMakeup) makeup += autoMakeupDb(threshold_.target, slope, knee_.target, range);
        makeup_.target = makeup;
        mix_.target = std::clamp(p_.mix, 0.0f, 1.0f);
        output_.target = dbToGain(p_.outputDb);
        rms_.target = std::clamp(p_.rms, 0.0f, 1.0f);
        link_.target = std::clamp(p_.link, 0.0f, 1.0f);
        tilt_.target = std::clamp(p_.keyTiltDb, -12.0f, 12.0f);
        listen_.target = p_.listen ? 1.0f : 0.0f;
        clip_.target = p_.clip ? 1.0f : 0.0f;
        auto_.target = p_.autoRelease ? 1.0f : 0.0f;
        for (int k = 0; k < 3; ++k) character_[static_cast<size_t>(k)].target = static_cast<int>(p_.character) == k ? 1.0f : 0.0f;
        logAttack_.target = std::log(std::clamp(p_.attackMs, 0.01f, 1000.0f));
        // With automatic release the detectors let go four times faster, and the slow memory sets the long release.
        const float release = std::clamp(p_.releaseMs, 1.0f, 10000.0f);
        logRelease_.target = std::log(p_.autoRelease ? std::max(5.0f, 0.25f * release) : release);
        slowReleaseMs_ = release;
        logHighPass_.target = std::log(std::clamp(p_.keyHighPassHz, 10.0f, 2000.0f));
        // Feedback answers faster the steeper the ratio; its attack is lengthened by as much to keep the knob honest.
        glueAttackScale_ = slope < 0.0f ? std::min(20.0f, 1.0f / std::max(0.05f, 1.0f + slope)) : 1.0f;
    }

    void snapControls() noexcept {
        setTargets();
        for (auto* gl : { &threshold_, &slope_, &knee_, &range_, &makeup_, &mix_, &output_, &rms_, &link_, &tilt_, &external_, &listen_, &clip_, &auto_,
                          &logAttack_, &logRelease_, &logHighPass_ })
            gl->snap(gl->target);
        for (auto& gl : character_) gl.snap(gl.target);
        external_.snap(0.0f);
        updateSlowControls();
    }

    // Envelope times and the key filter, which need an exp or a tan, move every kSub samples.
    void updateSlowControls() noexcept {
        const double attackMs = std::exp(static_cast<double>(logAttack_.next()));
        const double releaseMs = std::exp(static_cast<double>(logRelease_.next()));
        attack_ = poleFor(attackMs, fs_);
        attackHalf_ = poleFor(0.5 * attackMs, fs_);
        glueAttack_ = poleFor(attackMs * glueAttackScale_, fs_);
        release_ = poleFor(releaseMs, fs_);
        slowRelease_ = poleFor(slowReleaseMs_, fs_);
        const float hz = std::exp(logHighPass_.next());
        for (auto& hp : highPass_) hp.set(hz, fs_);
        // The tilt pivots around 1 kHz: lows down and highs up by half the setting each.
        for (int s = 0; s < kSub; ++s) tilt_.next(); // the glide keeps the time it would have per sample
        const float tilt = tilt_.value;
        tiltLowGain_ = dbToGain(-0.5f * tilt);
        tiltHighGain_ = dbToGain(0.5f * tilt);
    }

    static bool away(float target, float now) noexcept { return std::abs(target) > std::abs(now); }

    // Solves g = a * previous + (1 - a) * h(level + g) for g. The right side falls as g rises, so there is
    // exactly one answer; Newton finds it in a few steps and bisection keeps it inside the bracket.
    float solveWith(float level, float previous, float a, float threshold, float slope, float knee, float range) const noexcept {
        const float span = (1.0f - a) * range;
        float lo = a * previous - span, hi = a * previous + span;
        float g = std::clamp(previous, lo, hi);
        for (int it = 0; it < 16; ++it) {
            float d = 0.0f;
            const float h = feedbackGainDb(level + g, threshold, slope, knee, range, d);
            const float f = g - a * previous - (1.0f - a) * h;
            if (std::abs(f) < 1e-5f) break;
            if (f > 0.0f) hi = g;
            else lo = g;
            const float fd = 1.0f - (1.0f - a) * d;
            float next = g - f / std::max(fd, 1e-3f);
            if (!(next > lo && next < hi)) next = 0.5f * (lo + hi);
            g = next;
        }
        return g;
    }

    float solveFeedback(float level, float previous, float threshold, float slope, float knee, float range) const noexcept {
        const float attacked = solveWith(level, previous, glueAttack_, threshold, slope, knee, range);
        if (away(attacked, previous) || attacked == previous) return attacked;
        return solveWith(level, previous, release_, threshold, slope, knee, range);
    }

    // Reads the lookahead lines. A change of lookahead crossfades from the old delay to the new one.
    void readDelayed(float* main, float* key) noexcept {
        if (delayFade_ == 0 && delayTarget_ != delayNow_) {
            delayOld_ = delayNow_;
            delayNow_ = delayTarget_;
            delayFade_ = delayFadeLength_;
        }
        const int now = (write_ - delayNow_) & mask_;
        if (delayFade_ > 0) {
            const int old = (write_ - delayOld_) & mask_;
            const float w = static_cast<float>(delayFade_) / static_cast<float>(delayFadeLength_);
            for (int c = 0; c < 2; ++c) {
                const auto& m = lines_[static_cast<size_t>(c)];
                const auto& k = lines_[static_cast<size_t>(c + 2)];
                main[c] = m[static_cast<size_t>(now)] + w * (m[static_cast<size_t>(old)] - m[static_cast<size_t>(now)]);
                key[c] = k[static_cast<size_t>(now)] + w * (k[static_cast<size_t>(old)] - k[static_cast<size_t>(now)]);
            }
            --delayFade_;
        } else {
            for (int c = 0; c < 2; ++c) {
                main[c] = lines_[static_cast<size_t>(c)][static_cast<size_t>(now)];
                key[c] = lines_[static_cast<size_t>(c + 2)][static_cast<size_t>(now)];
            }
        }
    }

    double fs_ = 48000.0;
    Params p_;
    bool primed_ = false;

    std::array<std::vector<float>, 4> lines_; // main L, R, key L, R
    int mask_ = 0, write_ = 0;
    int delayNow_ = 0, delayOld_ = 0, delayTarget_ = 0, delayFade_ = 0, delayFadeLength_ = 1;

    Glide threshold_, slope_, knee_, range_, makeup_, mix_, output_, rms_, link_, tilt_, external_, listen_, clip_, auto_;
    std::array<Glide, 3> character_;
    Glide logAttack_, logRelease_, logHighPass_;
    int sub_ = 0;

    float attack_ = 0.0f, attackHalf_ = 0.0f, glueAttack_ = 0.0f, release_ = 0.0f, slowRelease_ = 0.0f;
    float slowReleaseMs_ = 120.0f, glueAttackScale_ = 1.0f;
    float rmsPole_ = 0.0f, buildPole_ = 0.0f;
    float tiltK_ = 0.0f, tiltLowGain_ = 1.0f, tiltHighGain_ = 1.0f;

    std::array<KeyHighPass, 2> highPass_;
    float tiltLow_[2] = {}, ms_[2] = {};
    float clean_[2] = {}, punch1_[2] = {}, punch2_[2] = {}, glue_[2] = {}, slow_[2] = {}, gain_[2] = {};

    std::array<Oversampler, 2> oversampler_;

    int frameLength_ = 240, frameAt_ = 0;
    Frame frame_;
    float lastGain_ = 0.0f;
};

} // namespace doc
