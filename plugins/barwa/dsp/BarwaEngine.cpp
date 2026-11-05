#include "BarwaEngine.h"

namespace barwa {

namespace {

using wers::kPi;

constexpr double kLookSeconds = 0.004;
constexpr double kSmoothSeconds = 0.02;
constexpr double kGateSeconds = 0.005;
constexpr double kSettleSeconds = 0.01; // shelves and band filters forget their past in this time
constexpr double kDcHz = 12.0, kTiltHz = 1000.0, kTiltDb = 6.0;
constexpr float kFullDriveDb = 3.0f; // the difference grows in the first 3 dB of the slider
constexpr float kStopShare = 0.35f;
constexpr float kInputLimit = 64.0f; // anything beyond +36 dBFS is not audio

float approach(float value, float target, float coef, float snap) noexcept {
    const float next = value + (target - value) * coef;
    return std::abs(target - next) < snap ? target : next;
}

float differenceAmount(float driveDb) noexcept {
    const float t = std::clamp(driveDb / kFullDriveDb, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float cleanValue(float value, float low, float high, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

void DcBlock::prepare(double fs) noexcept {
    pole_ = 1.0 - 2.0 * kPi * kDcHz / fs;
    gain_ = 0.5 * (1.0 + pole_);
    reset();
}

void Tilt::prepare(double fs) noexcept {
    warp_ = std::tan(kPi * kTiltHz / fs);
    set(0.0f);
    reset();
}

void Tilt::set(float tone) noexcept {
    const double k = std::pow(10.0, kTiltDb * static_cast<double>(tone) / 20.0);
    const double denominator = 1.0 + k * warp_;
    b0_ = (k + warp_) / denominator;
    b1_ = (warp_ - k) / denominator;
    a1_ = (k * warp_ - 1.0) / denominator;
}

bool BarwaEngine::Gate::advance(bool changeWanted, float step, int settleTicks) noexcept {
    switch (phase) {
        case Phase::Open:
            if (changeWanted) phase = Phase::Closing;
            break;
        case Phase::Closing:
            level -= step;
            if (level <= 0.0f) {
                level = 0.0f;
                wait = settleTicks;
                phase = Phase::Settling;
                return true;
            }
            break;
        case Phase::Settling:
            if (--wait <= 0) phase = Phase::Opening;
            break;
        case Phase::Opening:
            if (changeWanted) {
                phase = Phase::Closing;
                break;
            }
            level += step;
            if (level >= 1.0f) openNow();
            break;
    }
    return false;
}

void BarwaEngine::prepare(double fs, int qualityLog2) {
    fs_ = fs;
    const int log2 = wers::effectiveFactorLog2(qualityLog2, fs);
    lookSamples_ = static_cast<int>(std::lround(fs * kLookSeconds));
    controlStep_ = std::max(1, static_cast<int>(std::lround(32.0 * fs / 48000.0)));
    perSample_ = 1.0f / static_cast<float>(controlStep_);
    const double controlRate = fs / controlStep_;
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, controlRate));
    gateStep_ = 1.0f / std::max(1.0f, static_cast<float>(kGateSeconds * controlRate));
    settleTicks_ = std::max(1, static_cast<int>(std::lround(kSettleSeconds * controlRate)));
    for (auto& c : channel_) {
        c.look.prepare(lookSamples_);
        c.split.prepare(fs);
        c.dc.prepare(fs);
        c.tilt.prepare(fs);
        for (auto& b : c.band) {
            b.oversampler.prepare(log2);
            b.shaper.prepare(fs * b.oversampler.factor());
            factor_ = b.oversampler.factor();
            oversamplingLatency_ = b.oversampler.latencySamples();
            b.dry.prepare(oversamplingLatency_);
        }
    }
    classifier_.prepare(fs);
    hop_ = std::max(1, static_cast<int>(std::lround(classifier_.hopSeconds() * fs)));
    perHopSample_ = 1.0f / static_cast<float>(hop_);
    levelMatch_.prepare(fs);
    reset();
}

void BarwaEngine::reset() noexcept {
    for (auto& c : channel_) {
        c.look.reset();
        c.split.reset();
        c.dc.reset();
        c.tilt.reset();
        for (auto& b : c.band) {
            b.oversampler.reset();
            b.shaper.reset();
            b.dry.reset();
        }
    }
    classifier_.reset();
    levelMatch_.reset();
    matchGain_ = 1.0f;
    controlLeft_ = 0;
    snapPending_ = true;
}

void BarwaEngine::setParams(const BarwaParams& p) noexcept {
    params_ = p;
    params_.bands = std::clamp(p.bands, 1, 3);
    params_.f1 = cleanValue(p.f1, 20.0f, 20000.0f, 200.0f);
    params_.f2 = cleanValue(p.f2, 20.0f, 20000.0f, 3000.0f);
    params_.mask = cleanValue(p.mask, 0.0f, 1.0f, 0.0f);
    params_.tone = cleanValue(p.tone, -1.0f, 1.0f, 0.0f);
    params_.mix = cleanValue(p.mix, 0.0f, 1.0f, 1.0f);
    params_.outputDb = cleanValue(p.outputDb, -96.0f, 48.0f, 0.0f);
    for (auto& b : params_.band) {
        b.driveDb = cleanValue(b.driveDb, 0.0f, 48.0f, 0.0f);
        b.mix = cleanValue(b.mix, 0.0f, 1.0f, 1.0f);
    }
}

void BarwaEngine::applyBandCount(int bands) noexcept {
    for (auto& c : channel_) {
        c.split.setBands(bands);
        for (int i = bands_; i < bands; ++i) {
            auto& b = c.band[static_cast<size_t>(i)];
            b.oversampler.reset();
            b.shaper.reset();
            b.dry.reset();
        }
    }
    bands_ = bands;
}

void BarwaEngine::snapToParams() noexcept {
    const auto& p = params_;
    snapPending_ = false;
    bands_ = p.bands;
    f1_ = p.f1;
    f2_ = p.f2;
    tone_ = toneApplied_ = p.tone;
    mask_ = p.mask;
    mix_ = p.mix;
    outputDb_ = p.outputDb;
    dryShare_ = p.delta ? 0.0f : 1.0f;
    autoShare_ = p.autoLevel ? 1.0f : 0.0f;
    bandCountGate_.openNow();
    bandCountRamp_.set(1.0f);
    mixRamp_.set(mix_);
    maskRamp_.set(1.0f - mask_);
    maskNow_.store(1.0f - mask_, std::memory_order_relaxed);
    outputRamp_.set(wers::dbToGain(outputDb_));
    dryRamp_.set(dryShare_);
    autoRamp_.set(autoShare_);
    for (auto& c : channel_) {
        c.split.setBands(bands_);
        c.split.setFrequencies(f1_, f2_);
        c.tilt.set(tone_);
    }
    anySolo_ = false;
    for (int i = 0; i < bands_; ++i) anySolo_ = anySolo_ || p.band[static_cast<size_t>(i)].solo;
    for (size_t i = 0; i < bandControl_.size(); ++i) {
        auto& b = bandControl_[i];
        const auto& q = p.band[i];
        b.character = q.character;
        b.gate.openNow();
        b.driveDb = q.driveDb;
        b.mix = q.mix;
        b.solo = !anySolo_ || q.solo ? 1.0f : 0.0f;
        b.gain.set(wers::dbToGain(b.driveDb));
        b.weight.set(b.mix * differenceAmount(b.driveDb) * b.solo);
        b.keep.set(b.solo);
        for (auto& c : channel_) c.band[i].shaper.setCharacter(b.character);
    }
}

void BarwaEngine::controlTick() noexcept {
    controlLeft_ = controlStep_;
    if (snapPending_) {
        snapToParams();
        return;
    }
    const auto& p = params_;

    if (bandCountGate_.advance(p.bands != bands_, gateStep_, settleTicks_)) applyBandCount(p.bands);
    bandCountRamp_.aim(bandCountGate_.level, perSample_);

    const float f1 = approach(f1_, p.f1, smooth_, 0.01f), f2 = approach(f2_, p.f2, smooth_, 0.1f);
    if (f1 != f1_ || f2 != f2_) {
        f1_ = f1;
        f2_ = f2;
        for (auto& c : channel_) c.split.setFrequencies(f1_, f2_);
    }
    tone_ = approach(tone_, p.tone, smooth_, 1.0e-4f);
    if (tone_ != toneApplied_) {
        toneApplied_ = tone_;
        for (auto& c : channel_) c.tilt.set(tone_);
    }
    mask_ = approach(mask_, p.mask, smooth_, 1.0e-5f);
    mix_ = approach(mix_, p.mix, smooth_, 1.0e-5f);
    outputDb_ = approach(outputDb_, p.outputDb, smooth_, 1.0e-3f);
    dryShare_ = approach(dryShare_, p.delta ? 0.0f : 1.0f, smooth_, 1.0e-5f);
    autoShare_ = approach(autoShare_, p.autoLevel ? 1.0f : 0.0f, smooth_, 1.0e-5f);
    mixRamp_.aim(mix_, perSample_);
    outputRamp_.aim(wers::dbToGain(outputDb_), perSample_);
    dryRamp_.aim(dryShare_, perSample_);
    autoRamp_.aim(autoShare_, perSample_);

    anySolo_ = false;
    for (int i = 0; i < bands_; ++i) anySolo_ = anySolo_ || p.band[static_cast<size_t>(i)].solo;
    for (size_t i = 0; i < bandControl_.size(); ++i) {
        auto& b = bandControl_[i];
        const auto& q = p.band[i];
        if (b.gate.advance(q.character != b.character, gateStep_, settleTicks_)) {
            b.character = q.character;
            for (auto& c : channel_) c.band[i].shaper.setCharacter(b.character);
        }
        b.driveDb = approach(b.driveDb, q.driveDb, smooth_, 1.0e-3f);
        b.mix = approach(b.mix, q.mix, smooth_, 1.0e-5f);
        b.solo = approach(b.solo, !anySolo_ || q.solo ? 1.0f : 0.0f, smooth_, 1.0e-5f);
        b.gain.aim(wers::dbToGain(b.driveDb), perSample_);
        b.weight.aim(b.mix * differenceAmount(b.driveDb) * b.solo * b.gate.level, perSample_);
        b.keep.aim(b.solo, perSample_);
    }
}

void BarwaEngine::aimMask() noexcept {
    const auto& m = classifier_.current();
    const float share = std::clamp(m.vowel + kStopShare * m.stop, 0.0f, 1.0f);
    const float target = (1.0f - mask_) + mask_ * share;
    maskRamp_.aim(target, perHopSample_);
    maskNow_.store(target, std::memory_order_relaxed);
}

void BarwaEngine::process(float* const* channels, int numChannels, int numSamples) noexcept {
    const wers::ScopedFlushDenormals noDenormals;
    const int count = std::min(numChannels, 2);
    if (count <= 0) return;
    std::array<float, 8> frame{};
    for (int i = 0; i < numSamples; ++i) {
        if (controlLeft_ <= 0) controlTick();
        --controlLeft_;

        std::array<float, 2> in{};
        bool garbage = false;
        for (int c = 0; c < count; ++c) {
            const float v = channels[c][i];
            if (std::abs(v) <= kInputLimit) { // also false for NaN
                in[static_cast<size_t>(c)] = v;
            } else {
                garbage = true;
            }
        }
        if (garbage) {
            levelMatch_.reset();
            matchGain_ = 1.0f;
        }
        const float mono = count == 2 ? 0.5f * (in[0] + in[1]) : in[0];
        if (classifier_.push(mono)) aimMask();

        std::array<float, 2> dry{}, extra{};
        for (size_t c = 0; c < static_cast<size_t>(count); ++c) {
            auto& channel = channel_[c];
            float low = 0.0f, mid = 0.0f, high = 0.0f;
            channel.split.process(channel.look.process(in[c]), low, mid, high);
            const std::array<float, 3> parts{ low, bands_ == 3 ? mid : high, high };
            float added = 0.0f, untouched = 0.0f;
            for (size_t b = 0; b < static_cast<size_t>(bands_); ++b) {
                auto& audio = channel.band[b];
                const auto& control = bandControl_[b];
                audio.oversampler.up(parts[b], frame.data());
                for (int k = 0; k < factor_; ++k) frame[static_cast<size_t>(k)] = audio.shaper.tick(frame[static_cast<size_t>(k)], control.gain.now);
                const float wet = audio.oversampler.down(frame.data());
                const float delayed = audio.dry.process(parts[b]);
                untouched += control.keep.now * delayed;
                added += control.weight.now * (wet - delayed);
            }
            dry[c] = untouched;
            extra[c] = mixRamp_.now * maskRamp_.now * static_cast<float>(channel.tilt.tick(channel.dc.tick(static_cast<double>(added))));
        }

        // A soloed band says nothing about the level of the whole, so the gain holds while soloing.
        if (!anySolo_) {
            const float dryMono = count == 2 ? 0.5f * (dry[0] + dry[1]) : dry[0];
            const float extraMono = count == 2 ? 0.5f * (extra[0] + extra[1]) : extra[0];
            matchGain_ = levelMatch_.push(dryMono, dryMono + extraMono);
        }
        const float gain = (1.0f + autoRamp_.now * (matchGain_ - 1.0f)) * outputRamp_.now * bandCountRamp_.now;
        for (size_t c = 0; c < static_cast<size_t>(count); ++c)
            channels[c][i] = (dryRamp_.now * dry[c] + extra[c]) * gain;

        for (auto& b : bandControl_) {
            b.gain.now += b.gain.step;
            b.weight.now += b.weight.step;
            b.keep.now += b.keep.step;
        }
        mixRamp_.now += mixRamp_.step;
        maskRamp_.now += maskRamp_.step;
        outputRamp_.now += outputRamp_.step;
        dryRamp_.now += dryRamp_.step;
        autoRamp_.now += autoRamp_.step;
        bandCountRamp_.now += bandCountRamp_.step;
    }
}

} // namespace barwa
