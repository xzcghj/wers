#include "RefrenEngine.h"

namespace refren {

namespace {

constexpr double kLatencySeconds = 0.030, kSmoothSeconds = 0.02, kGateSeconds = 0.02, kSettleSeconds = 0.01, kMeterSeconds = 0.05;
constexpr int kOversamplingLatency = 49, kShortestRead = 2, kAllVoices = 32;
constexpr float kInputLimit = 64.0f; // anything beyond +36 dBFS is not audio

float approach(float value, float target, float coef, float snap) noexcept {
    const float next = value + (target - value) * coef;
    return std::abs(target - next) < snap ? target : next;
}

float cleanValue(float value, float low, float high, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

RefrenParams cleaned(const RefrenParams& p) noexcept {
    RefrenParams c = p;
    c.amount = cleanValue(p.amount, 0.0f, 1.0f, 1.0f);
    c.widthScale = cleanValue(p.widthScale, 0.0f, 2.0f, 1.0f);
    c.timeScale = cleanValue(p.timeScale, 0.0f, 2.0f, 1.0f);
    c.tilt = cleanValue(p.tilt, -1.0f, 1.0f, 0.0f);
    c.grainDb = cleanValue(p.grainDb, 0.0f, 36.0f, 0.0f);
    c.motionRate = cleanValue(p.motionRate, 0.05f, 10.0f, 1.0f);
    c.motionDepth = cleanValue(p.motionDepth, 0.0f, 1.0f, 0.0f);
    c.echoMix = cleanValue(p.echoMix, 0.0f, 1.0f, 0.0f);
    c.echoFeedback = cleanValue(p.echoFeedback, 0.0f, 0.9f, 0.25f);
    c.echoToneHz = cleanValue(p.echoToneHz, 500.0f, 12000.0f, 6000.0f);
    c.verbMix = cleanValue(p.verbMix, 0.0f, 1.0f, 0.0f);
    c.verbSize = cleanValue(p.verbSize, 0.0f, 1.0f, 0.5f);
    c.verbDecay = cleanValue(p.verbDecay, 0.2f, 8.0f, 1.5f);
    c.verbToneHz = cleanValue(p.verbToneHz, 1000.0f, 16000.0f, 6000.0f);
    c.verbPreMs = cleanValue(p.verbPreMs, 0.0f, 80.0f, 10.0f);
    c.verbShimmer = cleanValue(p.verbShimmer, 0.0f, 1.0f, 0.0f);
    c.duck = cleanValue(p.duck, 0.0f, 1.0f, 0.5f);
    c.sendDry = cleanValue(p.sendDry, 0.0f, 1.0f, 0.0f);
    c.consonants = cleanValue(p.consonants, 0.0f, 1.0f, 1.0f);
    c.dryDb = cleanValue(p.dryDb, -48.0f, 0.0f, 0.0f);
    c.trimDb = cleanValue(p.trimDb, -24.0f, 12.0f, 0.0f);
    c.outputDb = cleanValue(p.outputDb, -24.0f, 24.0f, 0.0f);
    for (auto& l : c.layer) {
        l.voices = std::clamp(l.voices, 1, Layer::kMaxVoices);
        l.semis = std::clamp(l.semis, -24, 24);
        l.formant = cleanValue(l.formant, -12.0f, 12.0f, 0.0f);
        l.levelDb = cleanValue(l.levelDb, -48.0f, 6.0f, -6.0f);
        l.width = cleanValue(l.width, 0.0f, 1.0f, 0.5f);
        l.timeMs = cleanValue(l.timeMs, 0.0f, 40.0f, 12.0f);
        l.detuneCents = cleanValue(l.detuneCents, 0.0f, 50.0f, 8.0f);
        l.lowHz = cleanValue(l.lowHz, 20.0f, 2000.0f, 20.0f);
        l.highHz = cleanValue(l.highHz, 1000.0f, 20000.0f, 20000.0f);
        l.note = std::clamp(l.note, 36, 72);
    }
    return c;
}

bool sameStructure(const LayerParams& a, const LayerParams& b) noexcept {
    return a.on == b.on && a.source == b.source && a.voices == b.voices && a.semis == b.semis && a.formant == b.formant && a.note == b.note;
}

} // namespace

void RefrenEngine::prepare(double fs) {
    fs_ = fs;
    latency_ = static_cast<int>(std::lround(kLatencySeconds * fs));
    shiftedDelay_ = latency_ - kOversamplingLatency - kShortestRead;
    controlStep_ = std::max(1, static_cast<int>(std::lround(32.0 * fs / 48000.0)));
    perSample_ = 1.0f / static_cast<float>(controlStep_);
    const double controlRate = fs / controlStep_;
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, controlRate));
    gateStep_ = static_cast<float>(1.0 / (kGateSeconds * controlRate));
    settleTicks_ = std::max(1, static_cast<int>(std::lround(kSettleSeconds * controlRate)));
    meterFall_ = static_cast<float>(1.0 - wers::onePoleCoeff(kMeterSeconds, controlRate));

    voice_.prepare(fs);
    mask_.prepare(fs);
    whisper_.prepare(fs);
    maskedDelay_.prepare(mask_.lookAheadSamples());
    uint32_t seed = 11u;
    for (auto& l : layers_) {
        l.layer.prepare(fs, seed);
        seed += 7u;
        l.psola.prepare(fs, shiftedDelay_);
        l.sub.prepare(fs, shiftedDelay_);
    }
    for (auto& d : dryDelay_) d.prepare(latency_);
    for (auto& d : busDelay_) d.prepare(kOversamplingLatency);
    reset();
}

void RefrenEngine::reset() noexcept {
    voice_.reset();
    mask_.reset();
    whisper_.reset();
    maskedDelay_.reset();
    for (auto& l : layers_) {
        l.layer.reset();
        l.psola.reset();
        l.sub.reset();
        l.lowCut.reset();
        l.highCut.reset();
        l.loudest = 0.0f;
    }
    for (auto& d : dryDelay_) d.reset();
    for (auto& d : busDelay_) d.reset();
    gate_.openNow();
    gateRamp_.set(1.0f);
    snapPending_ = true;
    controlLeft_ = 0;
    for (auto& v : layerLevel_) v.store(0.0f, std::memory_order_relaxed);
    maskNow_.store(1.0f, std::memory_order_relaxed);
}

void RefrenEngine::setParams(const RefrenParams& params) noexcept {
    wanted_ = cleaned(params);
}

bool RefrenEngine::structureChanged() const noexcept {
    for (size_t k = 0; k < layers_.size(); ++k)
        if (!sameStructure(wanted_.layer[k], layers_[k].applied)) return true;
    return false;
}

void RefrenEngine::applyStructure() noexcept {
    int left = kAllVoices;
    bool whisperNeeded = false;
    for (size_t k = 0; k < layers_.size(); ++k) {
        auto& l = layers_[k];
        const auto& p = wanted_.layer[k];
        l.applied = p;
        l.allowed = p.on ? std::min(p.voices, left) : 0;
        left -= l.allowed;
        activeVoices_[k].store(l.allowed, std::memory_order_relaxed);
        l.semis = static_cast<float>(p.semis);
        l.psola.reset();
        l.sub.reset();
        l.layer.reset();
        l.lowCut.reset();
        l.highCut.reset();
        PsolaSettings settings;
        settings.semitones = static_cast<float>(p.semis);
        settings.formantSemitones = p.formant;
        settings.fixedNote = p.source == Source::Robot;
        settings.noteHz = 440.0f * std::pow(2.0f, (static_cast<float>(p.note) - 69.0f) / 12.0f);
        l.psola.set(settings);
        switch (p.source) {
            case Source::Voice:
                l.kind = p.semis == 0 && p.formant == 0.0f ? StreamKind::Direct : StreamKind::Shifted;
                break;
            case Source::Robot:
                l.kind = StreamKind::Shifted;
                break;
            case Source::Sub:
                l.kind = StreamKind::Sub;
                break;
            case Source::Whisper:
                l.kind = StreamKind::Whisper;
                whisperNeeded = true;
                break;
        }
        switch (l.kind) {
            case StreamKind::Direct: l.streamDelay = mask_.lookAheadSamples(); break;
            case StreamKind::Shifted:
            case StreamKind::Sub: l.streamDelay = shiftedDelay_; break;
            case StreamKind::Whisper: l.streamDelay = whisper_.latencySamples(); break;
        }
    }
    if (whisperNeeded && !anyWhisper_) whisper_.reset();
    anyWhisper_ = whisperNeeded;
}

void RefrenEngine::controlTick() noexcept {
    controlLeft_ = controlStep_;
    const bool snap = snapPending_;
    if (snap) {
        applyStructure();
        amount_ = wanted_.amount;
        dryDb_ = wanted_.dryDb;
        trimDb_ = wanted_.trimDb;
        outputDb_ = wanted_.outputDb;
        consonants_ = wanted_.consonants;
        deltaShare_ = wanted_.delta ? 1.0f : 0.0f;
    } else {
        if (gate_.advance(structureChanged(), gateStep_, settleTicks_)) applyStructure();
        amount_ = approach(amount_, wanted_.amount, smooth_, 1.0e-5f);
        dryDb_ = approach(dryDb_, wanted_.dryDb, smooth_, 1.0e-3f);
        trimDb_ = approach(trimDb_, wanted_.trimDb, smooth_, 1.0e-3f);
        outputDb_ = approach(outputDb_, wanted_.outputDb, smooth_, 1.0e-3f);
        consonants_ = approach(consonants_, wanted_.consonants, smooth_, 1.0e-5f);
        deltaShare_ = approach(deltaShare_, wanted_.delta ? 1.0f : 0.0f, smooth_, 1.0e-5f);
    }
    mask_.setAmount(consonants_);
    const float dryGain = wers::dbToGain(dryDb_), trimGain = wers::dbToGain(trimDb_), outputGain = wers::dbToGain(outputDb_);
    if (snap) {
        gateRamp_.set(gate_.level);
        amountRamp_.set(amount_);
        dryRamp_.set(dryGain);
        trimRamp_.set(trimGain);
        outputRamp_.set(outputGain);
        dryShareRamp_.set(1.0f - deltaShare_);
    } else {
        gateRamp_.aim(gate_.level, perSample_);
        amountRamp_.aim(amount_, perSample_);
        dryRamp_.aim(dryGain, perSample_);
        trimRamp_.aim(trimGain, perSample_);
        outputRamp_.aim(outputGain, perSample_);
        dryShareRamp_.aim(1.0f - deltaShare_, perSample_);
    }
    for (size_t k = 0; k < layers_.size(); ++k) {
        auto& l = layers_[k];
        const auto& soft = wanted_.layer[k];
        LayerParams now = l.applied;
        now.levelDb = soft.levelDb;
        now.width = soft.width;
        now.timeMs = soft.timeMs;
        now.detuneCents = soft.detuneCents;
        l.layer.set(now, wanted_.widthScale, wanted_.timeScale, l.allowed, l.streamDelay);
        if (soft.lowHz != l.appliedLowHz) {
            l.appliedLowHz = soft.lowHz;
            l.lowCut.set(soft.lowHz, 0.7071, fs_);
        }
        if (soft.highHz != l.appliedHighHz) {
            l.appliedHighHz = soft.highHz;
            l.highCut.set(soft.highHz, 0.7071, fs_);
        }
        layerLevel_[k].store(l.loudest * l.layer.levelGain() * std::sqrt(static_cast<float>(std::max(1, l.allowed))) * gate_.level, std::memory_order_relaxed);
        l.loudest *= meterFall_;
    }
    maskNow_.store(mask_.now(), std::memory_order_relaxed);
    snapPending_ = false;
}

void RefrenEngine::process(float* const* channels, int numChannels, int numSamples) noexcept {
    const wers::ScopedFlushDenormals noDenormals;
    const int count = std::min(numChannels, 2);
    if (count <= 0) return;
    const int shiftedMaskDelay = shiftedDelay_ - mask_.lookAheadSamples();
    for (int i = 0; i < numSamples; ++i) {
        if (controlLeft_ <= 0) controlTick();
        --controlLeft_;

        std::array<float, 2> in{};
        for (int c = 0; c < count; ++c) {
            const float v = channels[c][i];
            if (std::abs(v) <= kInputLimit) in[static_cast<size_t>(c)] = v; // also false for NaN
        }
        const float mono = count == 2 ? 0.5f * (in[0] + in[1]) : in[0];
        voice_.push(mono);
        const float open = mask_.push(mono);
        const float masked = maskedDelay_.process(mono) * open;
        const float openShifted = mask_.delayed(shiftedMaskDelay);
        const float whispered = anyWhisper_ ? whisper_.tick(mono) : 0.0f;

        float wetLeft = 0.0f, wetRight = 0.0f;
        for (auto& l : layers_) {
            if (l.allowed == 0) continue;
            float stream = 0.0f;
            switch (l.kind) {
                case StreamKind::Direct: stream = masked; break;
                case StreamKind::Shifted: stream = l.psola.tick(voice_) * openShifted; break;
                case StreamKind::Sub: stream = l.sub.tick(voice_, l.semis) * openShifted; break;
                case StreamKind::Whisper: stream = whispered; break;
            }
            const auto filtered = static_cast<float>(l.highCut.tick(l.lowCut.tick(static_cast<double>(stream)).hp).lp);
            l.loudest = std::max(l.loudest, std::abs(filtered));
            l.layer.tick(filtered, wetLeft, wetRight);
        }
        // What the layers go through together comes here.
        wetLeft = busDelay_[0].process(wetLeft);
        wetRight = busDelay_[1].process(wetRight);

        const float gate = gateRamp_.now, amount = amountRamp_.now;
        const float dryGain = 1.0f + amount * (dryRamp_.now - 1.0f);
        const float trim = 1.0f + amount * (trimRamp_.now - 1.0f);
        const float last = trim * outputRamp_.now;
        const float keepDry = dryShareRamp_.now;
        if (count == 2) {
            channels[0][i] = (keepDry * dryGain * dryDelay_[0].process(in[0]) + amount * gate * wetLeft) * last;
            channels[1][i] = (keepDry * dryGain * dryDelay_[1].process(in[1]) + amount * gate * wetRight) * last;
        } else {
            channels[0][i] = (keepDry * dryGain * dryDelay_[0].process(in[0]) + amount * gate * 0.5f * (wetLeft + wetRight)) * last;
        }
        gateRamp_.now += gateRamp_.step;
        amountRamp_.now += amountRamp_.step;
        dryRamp_.now += dryRamp_.step;
        trimRamp_.now += trimRamp_.step;
        outputRamp_.now += outputRamp_.step;
        dryShareRamp_.now += dryShareRamp_.step;
    }
}

} // namespace refren
