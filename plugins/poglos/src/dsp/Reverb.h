#pragma once
// The WERS Pogłos engine: predelay, early reflections, input diffusion, a slow build, and a 16-line
// feedback delay network with modulated lines, allpasses inside the loop and a loss filter per line that
// sets the RT60 at low, middle and high frequencies. Nonlinear adds a gated cloud of taps and Shimmer a
// pitch shifter in the feedback. Header only, no JUCE, no allocation after prepare().
#include "Voicing.h"
#include <atomic>

namespace pog {

class Reverb {
public:
    static constexpr int kSub = 32;         // parameters are updated every kSub samples
    static constexpr int kHistory = 200;    // frames of band levels, kHistoryHz frames a second
    static constexpr int kHistoryHz = 50;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        fsf_ = static_cast<float>(fs_);
        const float ms = fsf_ / 1000.0f;
        float maxLine = 0.0f, maxEarly = 0.0f, maxMod = 0.0f;
        for (int a = 0; a < kAlgoCount; ++a) {
            const auto& v = voiceFor(static_cast<Algo>(a));
            maxLine = std::max(maxLine, v.lineMaxMs);
            maxEarly = std::max(maxEarly, v.earlyEndMs);
            maxMod = std::max(maxMod, v.modMs);
        }
        const int modRoom = static_cast<int>((maxMod * 4.0f + 1.0f) * ms) + 16;
        for (auto& l : lines_) l.allocate(static_cast<int>(maxLine * kMaxSizeScale * ms) + modRoom);
        for (auto& pair : loopAp_)
            for (auto& ap : pair) ap.allocate(static_cast<int>(25.0f * ms) + 2);
        for (int k = 0; k < 6; ++k) {
            inApL_[static_cast<size_t>(k)].allocate(static_cast<int>(16.0f * ms) + 2);
            inApR_[static_cast<size_t>(k)].allocate(static_cast<int>(16.0f * ms) + 2);
        }
        for (int k = 0; k < 2; ++k) {
            erApL_[static_cast<size_t>(k)].allocate(static_cast<int>(6.0f * ms) + 2);
            erApR_[static_cast<size_t>(k)].allocate(static_cast<int>(6.0f * ms) + 2);
            gateApL_[static_cast<size_t>(k)].allocate(static_cast<int>(8.0f * ms) + 2);
            gateApR_[static_cast<size_t>(k)].allocate(static_cast<int>(8.0f * ms) + 2);
        }
        preL_.allocate(static_cast<int>(kMaxPredelayMs * ms) + 8);
        preR_.allocate(static_cast<int>(kMaxPredelayMs * ms) + 8);
        erL_.allocate(static_cast<int>(maxEarly * kMaxSizeScale * ms) + 8);
        erR_.allocate(static_cast<int>(maxEarly * kMaxSizeScale * ms) + 8);
        buildL_.allocate(static_cast<int>(800.0f * ms));
        buildR_.allocate(static_cast<int>(800.0f * ms));
        gateL_.allocate(static_cast<int>((kMaxGate + 0.02f) * fsf_));
        gateR_.allocate(static_cast<int>((kMaxGate + 0.02f) * fsf_));
        shimBuf_.allocate(static_cast<int>(0.15f * fsf_));
        historyEvery_ = std::max(1, static_cast<int>(fsf_ / kHistoryHz));
        cur_ = target_;
        configure(target_.algo, target_.era, true);
        snapSmoothing();
    }

    void reset() {
        configure(target_.algo, target_.era, true);
        cur_ = target_;
        snapSmoothing();
    }

    void setParams(const Params& p) noexcept { target_ = p; }
    const Params& params() const noexcept { return target_; }

    void process(float* left, float* right, int n) noexcept {
        ScopedFlushDenormals flush;
        for (int at = 0; at < n; at += kSub) {
            const int m = std::min(kSub, n - at);
            updateBlock(m);
            for (int i = 0; i < m; ++i) tick(left[at + i], right[at + i]);
        }
        for (int b = 0; b < 3; ++b) bandDb_[static_cast<size_t>(b)].store(10.0f * std::log10(std::max(bandPow_[static_cast<size_t>(b)], 1e-12f)), std::memory_order_relaxed);
        duckDbOut_.store(duckDbNow_, std::memory_order_relaxed);
    }

    // For the display: wet level in three bands (dB), the gain taken by ducking, a short history.
    float bandLevelDb(int band) const noexcept { return bandDb_[static_cast<size_t>(std::clamp(band, 0, 2))].load(std::memory_order_relaxed); }
    float duckReductionDb() const noexcept { return duckDbOut_.load(std::memory_order_relaxed); }
    int historyCount() const noexcept { return historyCount_.load(std::memory_order_acquire); }
    float history(int band, int frame) const noexcept {
        return history_[static_cast<size_t>(std::clamp(band, 0, 2))][static_cast<size_t>(((frame % kHistory) + kHistory) % kHistory)].load(std::memory_order_relaxed);
    }
    Algo activeAlgo() const noexcept { return shownAlgo_.load(std::memory_order_relaxed); }
    float freezeAmount() const noexcept { return freezeShown_.load(std::memory_order_relaxed); }
    double sampleRate() const noexcept { return fs_; }

private:
    // --- setup ------------------------------------------------------------------------------------
    void configure(Algo algo, Era era, bool clear) noexcept {
        algo_ = algo;
        era_ = era;
        voice_ = &voiceFor(algo);
        const auto& v = *voice_;
        const float ms = fsf_ / 1000.0f;
        gate_ = algo == Algo::Nonlinear;
        shimmerOn_ = algo == Algo::Shimmer;

        const auto lens = lineLengthsMs(algo);
        loopAps_ = std::clamp(v.loopAllpasses, 0, 2);
        Rng signs(v.seed * 7u + 3u);
        for (int i = 0; i < kLines; ++i) {
            const auto s = static_cast<size_t>(i);
            base_[s] = lens[s] * ms;
            const auto ap = loopAllpassMs(algo, i);
            apSamples_[s] = 0.0f;
            for (int k = 0; k < 2; ++k) {
                const int d = std::max(1, static_cast<int>(std::lround(ap[static_cast<size_t>(k)] * ms)));
                loopAp_[s][static_cast<size_t>(k)].setDelay(d);
                if (k < loopAps_) apSamples_[s] += static_cast<float>(d);
            }
            inSign_[s] = signs.unit() > 0.5f ? 1.0f : -1.0f;
            outL_[s] = signs.unit() > 0.5f ? 0.25f : -0.25f;
            // The right output weights are the left ones times a balanced Hadamard row: orthogonal to the left.
            const int parity = bitCount(static_cast<unsigned>(i & 11));
            outR_[s] = outL_[s] * ((parity & 1) ? -1.0f : 1.0f);
            const float phase = 2.0f * kPiF * std::fmod(static_cast<float>(i) * 0.6180339f + 0.13f, 1.0f);
            lfoC_[s] = std::cos(phase);
            lfoS_[s] = std::sin(phase);
        }
        inStages_ = std::clamp(v.inputStages, 1, 6);
        const auto inL = inputAllpassMs(algo, 0), inR = inputAllpassMs(algo, 1);
        for (int k = 0; k < 6; ++k) {
            inApL_[static_cast<size_t>(k)].setDelay(std::max(1, static_cast<int>(std::lround(inL[static_cast<size_t>(k)] * ms))));
            inApR_[static_cast<size_t>(k)].setDelay(std::max(1, static_cast<int>(std::lround(inR[static_cast<size_t>(k)] * ms))));
        }
        const float erMs[2][2] = { { 1.73f, 3.07f }, { 2.29f, 3.71f } };
        const float gateMs[2][2] = { { 3.93f, 6.11f }, { 4.57f, 5.29f } };
        for (int k = 0; k < 2; ++k) {
            erApL_[static_cast<size_t>(k)].setDelay(static_cast<int>(std::lround(erMs[0][k] * ms)));
            erApR_[static_cast<size_t>(k)].setDelay(static_cast<int>(std::lround(erMs[1][k] * ms)));
            gateApL_[static_cast<size_t>(k)].setDelay(static_cast<int>(std::lround(gateMs[0][k] * ms)));
            gateApR_[static_cast<size_t>(k)].setDelay(static_cast<int>(std::lround(gateMs[1][k] * ms)));
        }
        tapCount_ = earlyTaps(algo, taps_);

        Rng build(v.seed * 13u + 5u);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kBuildTaps; ++k) {
                buildPos_[static_cast<size_t>(c)][static_cast<size_t>(k)] = (static_cast<float>(k) + 1.0f + 0.3f * build.bipolar()) / static_cast<float>(kBuildTaps);
                buildSign_[static_cast<size_t>(c)][static_cast<size_t>(k)] = build.unit() > 0.5f ? 1.0f : -1.0f;
            }
        Rng gate(v.seed * 17u + 9u);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kGateTaps; ++k) {
                gatePos_[static_cast<size_t>(c)][static_cast<size_t>(k)] = std::clamp((static_cast<float>(k) + 0.5f + 0.45f * gate.bipolar()) / static_cast<float>(kGateTaps), 0.004f, 1.0f);
                gateSign_[static_cast<size_t>(c)][static_cast<size_t>(k)] = gate.unit() > 0.5f ? 1.0f : -1.0f;
            }
        gateShapeFor_ = -1.0f;

        // Tone of the algorithm, then of the era.
        for (int c = 0; c < 2; ++c) {
            auto& eq = c == 0 ? voiceEqL_ : voiceEqR_;
            eq[0].set(Biquad::LowShelf, v.lowShelfHz, 0.7, v.lowShelfDb, fs_);
            eq[1].set(Biquad::Peak, v.peakHz, v.peakQ, v.peakDb, fs_);
            eq[2].set(Biquad::HighShelf, v.highShelfHz, 0.7, v.highShelfDb, fs_);
            auto& er = c == 0 ? eraEqL_ : eraEqR_;
            switch (era) {
            case Era::Eighties:
                // Early converters: a bright top that stops short of the air, and a coarse word length.
                er[0].set(Biquad::HighShelf, 4500.0, 0.7, 1.8, fs_);
                er[1].set(Biquad::LowPass, std::min(12500.0, 0.45 * fs_), 0.8, 0.0, fs_);
                er[2].bypass();
                break;
            case Era::Seventies:
                er[0].set(Biquad::HighPass, 45.0, 0.7, 0.0, fs_);
                // Tape-era bandwidth: two lowpasses, a steep and dark top.
                er[1].set(Biquad::LowPass, std::min(5600.0, 0.45 * fs_), 0.6, 0.0, fs_);
                er[2].set(Biquad::LowPass, std::min(7600.0, 0.45 * fs_), 0.9, 0.0, fs_);
                break;
            default:
                er[0].bypass();
                er[1].bypass();
                er[2].bypass();
                break;
            }
        }
        quantStep_ = era == Era::Eighties ? 1.0f / 4096.0f : 0.0f;
        eraModMul_ = era == Era::Seventies ? 1.6f : (era == Era::Eighties ? 1.2f : 1.0f);
        eraSine_ = era == Era::Seventies ? 0.25f : (era == Era::Eighties ? 1.0f : 0.75f);
        eraRandom_ = era == Era::Seventies ? 1.0f : (era == Era::Eighties ? 0.0f : 0.45f);
        eraJitter_ = era == Era::Seventies ? 0.05f * ms : 0.0f;

        if (clear) {
            for (auto& l : lines_) l.clear();
            for (auto& pair : loopAp_)
                for (auto& ap : pair) ap.clear();
            for (auto& ap : inApL_) ap.clear();
            for (auto& ap : inApR_) ap.clear();
            for (auto* ap : { &erApL_[0], &erApL_[1], &erApR_[0], &erApR_[1], &gateApL_[0], &gateApL_[1], &gateApR_[0], &gateApR_[1] }) ap->clear();
            for (auto* d : { &erL_, &erR_, &buildL_, &buildR_, &gateL_, &gateR_, &shimBuf_ }) d->clear();
            interpY_.fill(0.0f);
            for (auto& f : bassLp_) f.clear();
            for (auto& f : dampLp_) f.clear();
            for (auto& r : rnd1_) r = 0.0f;
            for (auto& r : rnd2_) r = 0.0f;
            for (auto& j : jitter_) j = 0.0f;
            shimOut_ = 0.0f;
            shimHp_.clear();
            shimLp_.clear();
        }
        for (auto* b : { &voiceEqL_, &voiceEqR_ })
            for (auto& q : *b) q.clear();
        for (auto* b : { &eraEqL_, &eraEqR_ })
            for (auto& q : *b) q.clear();
        const float scale = sizeScale(cur_.size);
        for (int i = 0; i < kLines; ++i) len_[static_cast<size_t>(i)] = base_[static_cast<size_t>(i)] * scale;
        for (int k = 0; k < tapCount_; ++k) erD_[static_cast<size_t>(k)] = std::max(1.0f, taps_[static_cast<size_t>(k)].ms * scale * ms);
        shownAlgo_.store(algo, std::memory_order_relaxed);
    }

    static int bitCount(unsigned x) noexcept {
        int c = 0;
        while (x) {
            c += static_cast<int>(x & 1u);
            x >>= 1;
        }
        return c;
    }

    void snapSmoothing() noexcept {
        preNow_ = std::round(std::clamp(cur_.predelayMs, 0.0f, kMaxPredelayMs) * fsf_ / 1000.0f);
        preStep_ = 0.0f;
        freezeAmt_ = cur_.freeze ? 1.0f : 0.0f;
        inGate_.snap(cur_.freeze ? 0.0f : 1.0f);
        switchGain_ = 1.0f;
        switching_ = 0;
        duckEnv_ = 0.0f;
        updateBlock(kSub);
        dry_.snap(dry_.value + dry_.step * kSub);
        wet_.snap(wet_.value + wet_.step * kSub);
        early_.snap(early_.value + early_.step * kSub);
        late_.snap(late_.value + late_.step * kSub);
        width_.snap(width_.value + width_.step * kSub);
        duckGain_.snap(1.0f);
        shimGain_.snap(shimGain_.value + shimGain_.step * kSub);
        for (int i = 0; i < kLines; ++i) lenStep_[static_cast<size_t>(i)] = 0.0f;
        for (int k = 0; k < tapCount_; ++k) erStep_[static_cast<size_t>(k)] = 0.0f;
        for (auto& b : bandPow_) b = 0.0f;
    }

    // --- per block --------------------------------------------------------------------------------
    float smoothing(float seconds, int m) const noexcept { return 1.0f - std::exp(-static_cast<float>(m) / (seconds * fsf_)); }

    void updateBlock(int m) noexcept {
        const auto& t = target_;
        if (switching_ == 0 && (t.algo != algo_ || t.era != era_)) switching_ = 1;
        switchStep_ = 1.0f / (0.025f * fsf_);

        const float fast = smoothing(0.03f, m), medium = smoothing(0.08f, m), slow = smoothing(0.3f, m);
        auto glide = [](float& v, float to, float k) { v += (to - v) * k; };
        glide(cur_.size, t.size, slow);
        glide(cur_.decay, std::clamp(t.decay, kMinDecay, kMaxDecay), medium);
        glide(cur_.bassMult, t.bassMult, medium);
        glide(cur_.bassHz, t.bassHz, medium);
        glide(cur_.dampHz, t.dampHz, medium);
        glide(cur_.highCutHz, t.highCutHz, fast);
        glide(cur_.lowCutHz, t.lowCutHz, fast);
        glide(cur_.modRate, t.modRate, medium);
        glide(cur_.modDepth, t.modDepth, medium);
        glide(cur_.diffEarly, t.diffEarly, fast);
        glide(cur_.diffLate, t.diffLate, fast);
        glide(cur_.attack, t.attack, medium);
        glide(cur_.shimmer, t.shimmer, fast);
        glide(cur_.shimmerSemis, t.shimmerSemis, fast);
        freezeAmt_ += ((t.freeze ? 1.0f : 0.0f) - freezeAmt_) * smoothing(0.04f, m);
        if (std::abs(freezeAmt_ - (t.freeze ? 1.0f : 0.0f)) < 1e-4f) freezeAmt_ = t.freeze ? 1.0f : 0.0f;
        freezeShown_.store(freezeAmt_, std::memory_order_relaxed);

        const auto& v = *voice_;
        const float ms = fsf_ / 1000.0f;
        const float scale = sizeScale(cur_.size);
        const float inv = 1.0f / static_cast<float>(m);

        // Line lengths glide with size; the loss of each loop follows its own length.
        const float fz = freezeAmt_;
        for (int i = 0; i < kLines; ++i) {
            const auto s = static_cast<size_t>(i);
            const float to = base_[s] * scale;
            lenStep_[s] = (to - len_[s]) * inv;
            const auto g = loopGainsFor(to + apSamples_[s], fsf_, cur_.decay, cur_.bassMult, cur_.bassHz, cur_.dampHz, fz);
            gMid_[s] = g.mid;
            kLow_[s] = g.kLow;
            kHigh_[s] = g.kHigh;
        }
        bassG_ = OnePole::coefficient(cur_.bassHz, fsf_);
        dampG_ = OnePole::coefficient(cur_.dampHz, fsf_);

        // Modulation: every line its own rate around the control, sine and smooth randomness by era.
        depth_ = std::clamp(cur_.modDepth, 0.0f, 1.0f) * v.modMs * eraModMul_ * ms * (1.0f - 0.5f * fz);
        for (int i = 0; i < kLines; ++i) {
            const auto s = static_cast<size_t>(i);
            const float rate = std::clamp(cur_.modRate, 0.01f, 10.0f) * (0.67f + 0.66f * std::fmod(static_cast<float>(i) * 0.6180339f, 1.0f));
            const float w = 2.0f * kPiF * rate / fsf_;
            lfoCw_[s] = std::cos(w);
            lfoSw_[s] = std::sin(w);
            rndK_[s] = 1.0f - std::exp(-2.0f * kPiF * rate * 1.3f / fsf_);
            rndEvery_[s] = std::max(8, static_cast<int>(fsf_ / (rate * 1.7f)));
            // Keep the phasor on the unit circle.
            const float r = 1.0f / std::sqrt(lfoC_[s] * lfoC_[s] + lfoS_[s] * lfoS_[s]);
            lfoC_[s] *= r;
            lfoS_[s] *= r;
        }
        modFloor_ = 0.08f * ms;
        jitterK_ = 1.0f - std::exp(-2.0f * kPiF * 25.0f / fsf_);

        const float inG = cur_.diffEarly * v.inputMax;
        for (auto& ap : inApL_) ap.setCoefficient(inG);
        for (auto& ap : inApR_) ap.setCoefficient(inG);
        const float erG = cur_.diffEarly * 0.62f;
        for (auto& ap : erApL_) ap.setCoefficient(erG);
        for (auto& ap : erApR_) ap.setCoefficient(-erG);
        const float loopG = cur_.diffLate * v.loopApMax;
        for (auto& pair : loopAp_) {
            pair[0].setCoefficient(loopG);
            pair[1].setCoefficient(-loopG * 0.9f);
        }
        for (auto& ap : gateApL_) ap.setCoefficient(0.62f * cur_.diffLate);
        for (auto& ap : gateApR_) ap.setCoefficient(-0.62f * cur_.diffLate);

        // Predelay glides (a small pitch bend while it moves) and lands on a whole sample.
        const float preTo = std::round(std::clamp(t.predelayMs, 0.0f, kMaxPredelayMs) * ms);
        float preNext = preNow_ + (preTo - preNow_) * smoothing(0.12f, m);
        if (std::abs(preTo - preNext) < 0.5f) preNext = preTo;
        preStep_ = (preNext - preNow_) * inv;

        for (int k = 0; k < tapCount_; ++k) erStep_[static_cast<size_t>(k)] = (std::max(1.0f, taps_[static_cast<size_t>(k)].ms * scale * ms) - erD_[static_cast<size_t>(k)]) * inv;

        // The build: taps over the build time with rising weights, normalised by energy.
        buildTime_ = (v.buildMs + 350.0f * cur_.attack) * std::sqrt(scale) * ms;
        buildMix_ = gate_ ? 0.0f : std::clamp(v.buildMix + cur_.attack, 0.0f, 1.0f);
        if (buildTime_ < 2.0f * ms) buildMix_ = 0.0f;
        {
            float sum = 0.0f;
            for (int k = 0; k < kBuildTaps; ++k) {
                const float w = std::pow(static_cast<float>(k + 1), 1.4f);
                buildW_[static_cast<size_t>(k)] = w;
                sum += w * w;
            }
            const float norm = 1.0f / std::sqrt(sum);
            for (auto& w : buildW_) w *= norm;
        }

        // The gate: its length follows decay, its envelope follows attack.
        gateLen_ = std::clamp(cur_.decay, 0.08f, kMaxGate) * fsf_;
        if (gate_ && std::abs(cur_.attack - gateShapeFor_) > 0.002f) shapeGate(cur_.attack);

        // Shimmer: the octave (or other interval) fed back, less of it for very long tails.
        const float shimTo = shimmerOn_ ? 0.6f * std::clamp(cur_.shimmer, 0.0f, 1.0f) * std::min(1.0f, std::pow(5.0f / cur_.decay, 0.2f)) * (1.0f - fz) : 0.0f;
        shimGain_.to(shimTo, m);
        shimRatio_ = std::pow(2.0f, std::clamp(cur_.shimmerSemis, -24.0f, 24.0f) / 12.0f);
        shimWindow_ = 0.06f * fsf_;
        shimHp_.g = OnePole::coefficient(350.0f, fsf_);
        shimLp_.g = OnePole::coefficient(7000.0f, fsf_);

        hiCutL_.set(cur_.highCutHz, 0.7071f, fsf_);
        hiCutR_.set(cur_.highCutHz, 0.7071f, fsf_);
        loCutL_.set(cur_.lowCutHz, 0.7071f, fsf_);
        loCutR_.set(cur_.lowCutHz, 0.7071f, fsf_);

        // Gains. Longer tails pile up more energy; take back half of that in dB so levels stay usable.
        const float mix = std::clamp(t.mix, 0.0f, 1.0f);
        const float comp = gate_ ? 1.0f : std::pow(2.0f / std::clamp(cur_.decay, 0.3f, 30.0f), 0.25f);
        dry_.to(std::cos(mix * 0.5f * kPiF), m);
        wet_.to(std::sin(mix * 0.5f * kPiF) * v.wet * comp, m);
        const float bal = std::clamp(t.balance, 0.0f, 1.0f);
        early_.to(std::min(1.0f, 2.0f * (1.0f - bal)), m);
        late_.to(std::min(1.0f, 2.0f * bal), m);
        width_.to(std::clamp(t.width, 0.0f, 1.5f), m);
        // Freeze closes the input over 30 ms, so what is held does not start with a cut.
        const float gateTo = t.freeze ? 0.0f : 1.0f, gateMove = static_cast<float>(m) / (0.03f * fsf_);
        inGate_.to(inGate_.value + std::clamp(gateTo - inGate_.value, -gateMove, gateMove), m);

        // Ducking from the dry input's envelope: up to 30 dB less wet while the input is loud.
        const float envDb = gainToDb(duckEnv_);
        duckDbNow_ = std::clamp(t.duck, 0.0f, 1.0f) * 30.0f * std::clamp((envDb + 45.0f) / 35.0f, 0.0f, 1.0f);
        duckGain_.to(dbToGain(-duckDbNow_), m);
        duckAtk_ = 1.0f - std::exp(-1.0f / (0.004f * fsf_));
        duckRel_ = 1.0f - std::exp(-1.0f / (0.25f * fsf_));
        meterK_ = 1.0f - std::exp(-1.0f / (0.06f * fsf_));
        meterLow_.g = OnePole::coefficient(250.0f, fsf_);
        meterHigh_.g = OnePole::coefficient(3500.0f, fsf_);
    }

    void shapeGate(float attack) noexcept {
        gateShapeFor_ = attack;
        for (int c = 0; c < 2; ++c) {
            double sum = 0.0;
            for (int k = 0; k < kGateTaps; ++k) {
                const float x = gatePos_[static_cast<size_t>(c)][static_cast<size_t>(k)];
                float g = gateEnvelope(attack, x);
                sum += static_cast<double>(g) * g;
                gateGain_[static_cast<size_t>(c)][static_cast<size_t>(k)] = g;
            }
            const float norm = static_cast<float>(1.0 / std::sqrt(std::max(sum, 1e-9)));
            for (int k = 0; k < kGateTaps; ++k) gateGain_[static_cast<size_t>(c)][static_cast<size_t>(k)] *= norm * gateSign_[static_cast<size_t>(c)][static_cast<size_t>(k)];
        }
    }

public:
    // The envelope of the gate across its length (x from 0 to 1): falling, flat, or rising like a reversed tail.
    static float gateEnvelope(float attack, float x) noexcept {
        const float falling = std::exp(-2.6f * x);
        const float rising = 0.05f + 0.95f * std::pow(x, 1.7f);
        const float a = std::clamp(attack, 0.0f, 1.0f);
        float g = a < 0.5f ? falling + (1.0f - falling) * (a * 2.0f) : 1.0f + (rising - 1.0f) * ((a - 0.5f) * 2.0f);
        if (x > 0.93f) g *= 0.5f + 0.5f * std::cos(kPiF * (x - 0.93f) / 0.07f);
        return g;
    }

private:
    // --- per sample -------------------------------------------------------------------------------
    void tick(float& left, float& right) noexcept {
        const float dryL = left, dryR = right;
        if (switching_ != 0) {
            if (switching_ == 1) {
                switchGain_ -= switchStep_;
                if (switchGain_ <= 0.0f) {
                    switchGain_ = 0.0f;
                    configure(target_.algo, target_.era, target_.algo != algo_);
                    switching_ = 2;
                }
            } else {
                switchGain_ += switchStep_;
                if (switchGain_ >= 1.0f) {
                    switchGain_ = 1.0f;
                    switching_ = 0;
                }
            }
        }

        const float a = std::max(std::abs(dryL), std::abs(dryR));
        duckEnv_ += (a > duckEnv_ ? duckAtk_ : duckRel_) * (a - duckEnv_);

        const float gIn = inGate_.next();
        preL_.write(dryL * gIn);
        preR_.write(dryR * gIn);
        preNow_ += preStep_;
        const float pl = preL_.readLinear(preNow_ + 1.0f);
        const float pr = preR_.readLinear(preNow_ + 1.0f);

        // Early reflections.
        erL_.write(pl);
        erR_.write(pr);
        float el = 0.0f, er = 0.0f;
        for (int k = 0; k < tapCount_; ++k) {
            const auto s = static_cast<size_t>(k);
            erD_[s] += erStep_[s];
            const float x = (taps_[s].source == 0 ? erL_ : erR_).readCubic(erD_[s] + 1.0f);
            el += x * taps_[s].gainL;
            er += x * taps_[s].gainR;
        }
        el = erApL_[1].process(erApL_[0].process(el));
        er = erApR_[1].process(erApR_[0].process(er));

        // Input diffusion.
        float dl = pl, dr = pr;
        for (int k = 0; k < inStages_; ++k) {
            dl = inApL_[static_cast<size_t>(k)].process(dl);
            dr = inApR_[static_cast<size_t>(k)].process(dr);
        }
        const auto& v = *voice_;
        dl += v.earlyFeed * el;
        dr += v.earlyFeed * er;

        // The build: the late part swells in over the build time.
        if (buildMix_ > 0.0f) {
            buildL_.write(dl);
            buildR_.write(dr);
            float sl = 0.0f, sr = 0.0f;
            for (int k = 0; k < kBuildTaps; ++k) {
                const auto s = static_cast<size_t>(k);
                sl += buildW_[s] * buildSign_[0][s] * buildL_.readCubic(buildPos_[0][s] * buildTime_ + 2.0f);
                sr += buildW_[s] * buildSign_[1][s] * buildR_.readCubic(buildPos_[1][s] * buildTime_ + 2.0f);
            }
            dl += buildMix_ * (sl - dl);
            dr += buildMix_ * (sr - dr);
        }

        const float sh = shimOut_;
        dl += sh;
        dr += sh;

        // The gate cloud.
        float gl = 0.0f, gr = 0.0f;
        if (gate_) {
            gateL_.write(dl);
            gateR_.write(dr);
            for (int k = 0; k < kGateTaps; ++k) {
                const auto s = static_cast<size_t>(k);
                gl += gateGain_[0][s] * gateL_.readCubic(gatePos_[0][s] * gateLen_ + 2.0f);
                gr += gateGain_[1][s] * gateR_.readCubic(gatePos_[1][s] * gateLen_ + 2.0f);
            }
            gl = gateApL_[1].process(gateApL_[0].process(gl));
            gr = gateApR_[1].process(gateApR_[0].process(gr));
        }

        // The network.
        float y[kLines];
        float lateL = 0.0f, lateR = 0.0f;
        const float sineMix = eraSine_, randMix = eraRandom_ * 1.7f;
        for (int i = 0; i < kLines; ++i) {
            const auto s = static_cast<size_t>(i);
            // Modulation.
            const float c = lfoC_[s] * lfoCw_[s] - lfoS_[s] * lfoSw_[s];
            lfoS_[s] = lfoS_[s] * lfoCw_[s] + lfoC_[s] * lfoSw_[s];
            lfoC_[s] = c;
            if (--rndCount_[s] <= 0) {
                rndCount_[s] = rndEvery_[s];
                rndTarget_[s] = rng_.bipolar();
            }
            rnd1_[s] += rndK_[s] * (rndTarget_[s] - rnd1_[s]);
            rnd2_[s] += rndK_[s] * (rnd1_[s] - rnd2_[s]);
            // A tiny random wander is always there: a perfectly still network would keep its modes.
            float mod = depth_ * (sineMix * lfoS_[s] + randMix * rnd2_[s]) + modFloor_ * rnd2_[s];
            if (eraJitter_ > 0.0f) {
                jitter_[s] += jitterK_ * (rng_.bipolar() - jitter_[s]);
                mod += eraJitter_ * 4.0f * jitter_[s];
            }
            len_[s] += lenStep_[s];
            // Allpass interpolation keeps every frequency at full level as the read point moves.
            const float d = std::max(2.0f, len_[s] + mod);
            const int whole = static_cast<int>(d - 0.618f);
            const float frac = d - static_cast<float>(whole);
            const float eta = (1.0f - frac) / (1.0f + frac);
            float x = eta * (lines_[s].read(whole) - interpY_[s]) + lines_[s].read(whole + 1);
            interpY_[s] = x;
            for (int k = 0; k < loopAps_; ++k) x = loopAp_[s][static_cast<size_t>(k)].process(x);
            // Loss: the middle gain with a low shelf (bass multiplier) and a high shelf (damping).
            bassLp_[s].g = bassG_;
            const float lowPart = bassLp_[s].lowpass(x);
            x += kLow_[s] * lowPart;
            dampLp_[s].g = dampG_;
            const float rest = dampLp_[s].lowpass(x);
            x += kHigh_[s] * (x - rest);
            x *= gMid_[s];
            y[i] = x;
            lateL += outL_[s] * x;
            lateR += outR_[s] * x;
        }
        hadamard16(y);
        for (int i = 0; i < kLines; ++i) {
            const auto s = static_cast<size_t>(i);
            float w = y[i] + inSign_[s] * ((i & 1) ? dr : dl);
            // A soft ceiling, far above anything musical, so nothing can run away.
            const float mag = std::abs(w);
            if (mag > 1.0f) w = std::copysign(1.0f + (mag - 1.0f) / (1.0f + (mag - 1.0f)), w);
            lines_[s].write(w);
        }

        // Shimmer: the tail pitched up by two crossfading read heads and fed back next sample.
        if (shimmerOn_) {
            const float in = 0.5f * (lateL + lateR);
            const float hp = in - shimHp_.lowpass(in);
            shimBuf_.write(hp);
            shimPhase_ += (1.0f - shimRatio_) / shimWindow_;
            shimPhase_ -= std::floor(shimPhase_);
            const float p2 = shimPhase_ + 0.5f - std::floor(shimPhase_ + 0.5f);
            const float h1 = shimBuf_.readLinear(shimPhase_ * shimWindow_ + 2.0f);
            const float h2 = shimBuf_.readLinear(p2 * shimWindow_ + 2.0f);
            const float w1 = std::sin(kPiF * shimPhase_), w2 = std::sin(kPiF * p2);
            float o = shimLp_.lowpass(h1 * w1 * w1 + h2 * w2 * w2) * shimGain_.next();
            const float mag = std::abs(o);
            if (mag > 0.5f) o = std::copysign(0.5f + (mag - 0.5f) / (1.0f + 2.0f * (mag - 0.5f)), o);
            shimOut_ = o;
        }

        if (gate_) {
            // The gate is what you hear; the network takes over only while frozen.
            const float k = freezeAmt_;
            lateL = gl * (1.0f - k) + lateL * k * 0.6f;
            lateR = gr * (1.0f - k) + lateR * k * 0.6f;
        }

        const float gEarly = early_.next(), gLate = late_.next();
        float wl = el * gEarly + lateL * gLate;
        float wr = er * gEarly + lateR * gLate;
        for (int k = 0; k < 3; ++k) {
            wl = voiceEqL_[static_cast<size_t>(k)].process(wl);
            wr = voiceEqR_[static_cast<size_t>(k)].process(wr);
        }
        for (int k = 0; k < 3; ++k) {
            wl = eraEqL_[static_cast<size_t>(k)].process(wl);
            wr = eraEqR_[static_cast<size_t>(k)].process(wr);
        }
        wl = loCutL_.highpass(hiCutL_.lowpass(wl));
        wr = loCutR_.highpass(hiCutR_.lowpass(wr));
        const float gWet = wet_.next() * switchGain_ * duckGain_.next();
        wl *= gWet;
        wr *= gWet;
        if (quantStep_ > 0.0f) {
            // Early digital: a short word length, heard as grain in the quiet end of the tail.
            wl = std::floor(wl / quantStep_ + 0.5f) * quantStep_;
            wr = std::floor(wr / quantStep_ + 0.5f) * quantStep_;
        }
        const float wid = width_.next();
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * wid;
        wl = mid + side;
        wr = mid - side;
        const float gDry = dry_.next();
        left = dryL * gDry + wl;
        right = dryR * gDry + wr;

        // Meters on the wet signal, before the mix.
        const float mono = mid;
        const float lo = meterLow_.lowpass(mono);
        const float hiRest = meterHigh_.lowpass(mono);
        const float bands[3] = { lo, hiRest - lo, mono - hiRest };
        for (int b = 0; b < 3; ++b) bandPow_[static_cast<size_t>(b)] += meterK_ * (bands[b] * bands[b] - bandPow_[static_cast<size_t>(b)]);
        if (++historyTick_ >= historyEvery_) {
            historyTick_ = 0;
            const int at = historyCount_.load(std::memory_order_relaxed);
            for (int b = 0; b < 3; ++b)
                history_[static_cast<size_t>(b)][static_cast<size_t>(at % kHistory)].store(10.0f * std::log10(std::max(bandPow_[static_cast<size_t>(b)], 1e-12f)), std::memory_order_relaxed);
            historyCount_.store(at + 1, std::memory_order_release);
        }
    }

    static constexpr int kBuildTaps = 8;
    using LineArray = std::array<float, kLines>;

    double fs_ = 48000.0;
    float fsf_ = 48000.0f;
    Params target_, cur_;
    Algo algo_ = Algo::Hall;
    Era era_ = Era::Now;
    const Voice* voice_ = &voiceFor(Algo::Hall);
    bool gate_ = false, shimmerOn_ = false;
    int switching_ = 0;
    float switchGain_ = 1.0f, switchStep_ = 0.001f;

    Delay preL_, preR_;
    float preNow_ = 0.0f, preStep_ = 0.0f;

    Delay erL_, erR_;
    std::array<EarlyTap, kMaxEarlyTaps> taps_{};
    std::array<float, kMaxEarlyTaps> erD_{}, erStep_{};
    int tapCount_ = 0;
    std::array<Allpass, 2> erApL_, erApR_;

    std::array<Allpass, 6> inApL_, inApR_;
    int inStages_ = 4;

    Delay buildL_, buildR_;
    std::array<std::array<float, kBuildTaps>, 2> buildPos_{}, buildSign_{};
    std::array<float, kBuildTaps> buildW_{};
    float buildTime_ = 0.0f, buildMix_ = 0.0f;

    std::array<Delay, kLines> lines_;
    LineArray base_{}, len_{}, lenStep_{}, apSamples_{}, interpY_{};
    std::array<std::array<Allpass, 2>, kLines> loopAp_;
    int loopAps_ = 1;
    LineArray gMid_{}, kLow_{}, kHigh_{};
    std::array<OnePole, kLines> bassLp_, dampLp_;
    float bassG_ = 0.1f, dampG_ = 0.5f;
    LineArray inSign_{}, outL_{}, outR_{};

    LineArray lfoC_{}, lfoS_{}, lfoCw_{}, lfoSw_{}, rnd1_{}, rnd2_{}, rndTarget_{}, rndK_{}, jitter_{};
    std::array<int, kLines> rndCount_{}, rndEvery_{};
    float depth_ = 0.0f, modFloor_ = 0.0f, jitterK_ = 0.0f;
    float eraModMul_ = 1.0f, eraSine_ = 0.75f, eraRandom_ = 0.45f, eraJitter_ = 0.0f;
    Rng rng_{ 4242u };

    Delay shimBuf_;
    float shimPhase_ = 0.0f, shimRatio_ = 2.0f, shimWindow_ = 2880.0f, shimOut_ = 0.0f;
    OnePole shimHp_, shimLp_;
    Ramp shimGain_;

    Delay gateL_, gateR_;
    std::array<std::array<float, kGateTaps>, 2> gatePos_{}, gateGain_{}, gateSign_{};
    std::array<Allpass, 2> gateApL_, gateApR_;
    float gateLen_ = 24000.0f, gateShapeFor_ = -1.0f;

    std::array<Biquad, 3> voiceEqL_, voiceEqR_;
    std::array<Biquad, 3> eraEqL_, eraEqR_;
    Svf hiCutL_, hiCutR_, loCutL_, loCutR_;
    float quantStep_ = 0.0f;

    Ramp dry_, wet_, early_, late_, width_, inGate_, duckGain_;
    float freezeAmt_ = 0.0f;
    float duckEnv_ = 0.0f, duckAtk_ = 0.01f, duckRel_ = 0.001f, duckDbNow_ = 0.0f;

    OnePole meterLow_, meterHigh_;
    float meterK_ = 0.001f;
    std::array<float, 3> bandPow_{};
    int historyEvery_ = 960, historyTick_ = 0;
    std::array<std::atomic<float>, 3> bandDb_{};
    std::atomic<float> duckDbOut_{ 0.0f }, freezeShown_{ 0.0f };
    std::atomic<Algo> shownAlgo_{ Algo::Hall };
    std::array<std::array<std::atomic<float>, kHistory>, 3> history_{};
    std::atomic<int> historyCount_{ 0 };
};

} // namespace pog
