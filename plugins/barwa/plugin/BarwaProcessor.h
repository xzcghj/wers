#pragma once
#include "BarwaEngine.h"
#include "FactoryPresets.h"
#include "Params.h"

namespace barwa {

class BarwaProcessor final : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater {
public:
    static constexpr int kStateVersion = 1;

    BarwaProcessor();
    ~BarwaProcessor() override;

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
    const juce::String getName() const override { return "WERS Barwa"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return static_cast<double>(getLatencySamples()) / fs_; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;
    void updateTrackProperties(const TrackProperties& properties) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }
    wers::ui::PresetStore& presets() noexcept { return presets_; }
    float maskNow() const noexcept { return engine_.maskNow(); }
    double hostRate() const noexcept { return fs_; }
    // Takes n samples of the input and the output for the display, if that many are waiting.
    bool popScope(float* in, float* out, int n) noexcept;
    // The highest output sample since the last call, as a linear value.
    float takeOutputPeak() noexcept { return outputPeak_.exchange(0.0f, std::memory_order_relaxed); }
    float uiScale() const noexcept { return uiScale_.load(); }
    void setUiScale(float scale) noexcept { uiScale_.store(scale); }
private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    int wantedQuality() const noexcept;
    void applyQuality();
    void prepareForQuality();
    BarwaParams readParams() const noexcept;
    void processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;
    void processChunk(float* const* channels, int count, int n, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    wers::ui::PresetStore presets_;
    std::atomic<float>* pBands_, *pXover1_, *pXover2_, *pMask_, *pTone_, *pMix_, *pAutoLevel_, *pOutput_, *pDelta_, *pQuality_, *pBypass_;
    std::array<std::atomic<float>*, 3> pChar_{}, pDrive_{}, pBandMix_{}, pSolo_{};

    double fs_ = 48000.0;
    static constexpr int kChunk = 1024;
    BarwaEngine engine_;
    int appliedQuality_ = 2;

    // Bypass crossfade with a matching delay, so toggling never clicks or shifts the audio in time.
    juce::AudioBuffer<float> dry_;
    std::array<wers::DelayLine, 2> dryDelay_{};
    float bypassMix_ = 0.0f, bypassStep_ = 0.001f;

    static constexpr int kScopeSize = 1 << 15;
    juce::AbstractFifo scopeFifo_{ kScopeSize };
    std::vector<float> scopeIn_, scopeOut_;

    std::atomic<float> outputPeak_{ 0.0f };
    std::atomic<bool> maskDecided_{ false };
    std::atomic<float> uiScale_{ 1.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BarwaProcessor)
};

} // namespace barwa
