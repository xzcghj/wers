#pragma once
#include "Params.h"
#include "PresetStore.h"
#include "RefrenEngine.h"

namespace refren {

class RefrenProcessor final : public juce::AudioProcessor {
public:
    static constexpr int kStateVersion = 1;

    RefrenProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "WERS Refren"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }
    wers::ui::PresetStore& presets() noexcept { return presets_; }
    float maskNow() const noexcept { return engine_.maskNow(); }
    float layerLevelNow(int layer) const noexcept { return engine_.layerLevelNow(layer); }
    int activeVoices(int layer) const noexcept { return engine_.activeVoices(layer); }
    // The highest output sample since the last call, as a linear value.
    float takeOutputPeak() noexcept { return outputPeak_.exchange(0.0f, std::memory_order_relaxed); }
    float uiScale() const noexcept { return uiScale_.load(); }
    void setUiScale(float scale) noexcept { uiScale_.store(scale); }
private:
    struct LayerValues {
        std::atomic<float>* on, *source, *voices, *semis, *formant, *level, *width, *time, *detune, *low, *high, *note;
    };
    void prepareAll();
    RefrenParams readParams() const noexcept;
    void processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;
    void processChunk(float* const* channels, int count, int n, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    wers::ui::PresetStore presets_;
    std::atomic<float>* pAmount_, *pWidth_, *pTime_, *pTilt_, *pGrain_, *pGrainChar_, *pMotion_, *pMotionRate_, *pMotionDepth_;
    std::atomic<float>* pEchoMix_, *pEchoTime_, *pEchoFeedback_, *pEchoPing_, *pEchoTone_;
    std::atomic<float>* pVerbMix_, *pVerbSize_, *pVerbDecay_, *pVerbTone_, *pVerbPre_, *pVerbShimmer_;
    std::atomic<float>* pDuck_, *pSendDry_, *pConsonants_, *pDry_, *pTrim_, *pOutput_, *pDelta_, *pBypass_;
    std::array<LayerValues, kLayers> pLayer_{};

    double fs_ = 48000.0, hostBpm_ = 0.0;
    static constexpr int kChunk = 1024;
    RefrenEngine engine_;

    // Bypass crossfade with a matching delay, so toggling never clicks or shifts the audio in time.
    juce::AudioBuffer<float> dry_;
    std::array<wers::DelayLine, 2> dryDelay_{};
    float bypassMix_ = 0.0f, bypassStep_ = 0.001f;

    std::atomic<float> outputPeak_{ 0.0f };
    std::atomic<float> uiScale_{ 1.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RefrenProcessor)
};

} // namespace refren
