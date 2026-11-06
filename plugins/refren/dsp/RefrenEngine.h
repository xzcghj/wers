#pragma once
// The whole sound of Refren: the dry voice, a fixed delay of 30 ms, up to four layers made from the mono sum of the
// input, and what they go through together. At an amount of zero the dry voice passes untouched. All memory is
// allocated in prepare(), and process() neither allocates nor locks.
#include "Layer.h"
#include "Psola.h"
#include "SubVoice.h"
#include "VoiceMask.h"
#include "WhisperVoice.h"
#include <atomic>

namespace refren {

class RefrenEngine {
public:
    void prepare(double fs);
    void reset() noexcept;
    void setParams(const RefrenParams& params) noexcept;
    void setTempo(double bpm) noexcept { bpm_ = bpm; }
    int latencySamples() const noexcept { return latency_; }
    void process(float* const* channels, int numChannels, int numSamples) noexcept;
    // Safe to read from any thread.
    float maskNow() const noexcept { return maskNow_.load(std::memory_order_relaxed); }
    float layerLevelNow(int layer) const noexcept { return layerLevel_[static_cast<size_t>(layer)].load(std::memory_order_relaxed); }
    int activeVoices(int layer) const noexcept { return activeVoices_[static_cast<size_t>(layer)].load(std::memory_order_relaxed); }
private:
    enum class StreamKind { Direct, Shifted, Sub, Whisper };
    struct LayerAudio {
        Layer layer;
        Psola psola;
        SubVoice sub;
        wers::Svf lowCut, highCut;
        StreamKind kind = StreamKind::Direct;
        LayerParams applied;
        int allowed = 0, streamDelay = 0;
        float appliedLowHz = 0.0f, appliedHighHz = 0.0f, semis = 0.0f, loudest = 0.0f;
    };
    void controlTick() noexcept;
    void applyStructure() noexcept;
    bool structureChanged() const noexcept;

    double fs_ = 48000.0, bpm_ = 120.0;
    int latency_ = 1440, shiftedDelay_ = 1389, controlStep_ = 32, controlLeft_ = 0, settleTicks_ = 15;
    float perSample_ = 1.0f / 32.0f, smooth_ = 0.05f, gateStep_ = 0.033f, meterFall_ = 0.99f;
    bool snapPending_ = true, anyWhisper_ = false;

    RefrenParams wanted_;
    VoiceAnalysis voice_;
    VoiceMask mask_;
    WhisperVoice whisper_;
    wers::DelayLine maskedDelay_;
    std::array<LayerAudio, 4> layers_;
    std::array<wers::DelayLine, 2> dryDelay_, busDelay_;
    wers::Gate gate_;
    wers::Ramp gateRamp_, amountRamp_, dryRamp_, trimRamp_, outputRamp_;
    float amount_ = 1.0f, dryDb_ = 0.0f, trimDb_ = 0.0f, outputDb_ = 0.0f, consonants_ = 1.0f, deltaShare_ = 0.0f;
    wers::Ramp dryShareRamp_;
    std::atomic<float> maskNow_{ 1.0f };
    std::array<std::atomic<float>, 4> layerLevel_{};
    std::array<std::atomic<int>, 4> activeVoices_{};
};

} // namespace refren
