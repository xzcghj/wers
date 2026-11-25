#pragma once
#include "FactoryPresets.h"
#include "Params.h"

namespace powiew {

class PowiewProcessor final : public juce::AudioProcessor {
public:
    static constexpr int kStateVersion = 1;

    PowiewProcessor();
    ~PowiewProcessor() override = default;

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
    const juce::String getName() const override { return "WERS Powiew"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.01; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    // VST3 hosts list the one program by this name; an empty one fails Steinberg's validator.
    const juce::String getProgramName(int) override { return "WERS Powiew"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }
    wers::ui::PresetStore& presets() noexcept { return presets_; }
    const AirEngine& engine() const noexcept { return engine_; }
    double sampleRate() const noexcept { return fs_; }

    // The editor's size travels with the project.
    float uiScale() const noexcept { return uiScale_.load(); }
    void setUiScale(float s) noexcept { uiScale_.store(s); }
private:
    void processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    wers::ui::PresetStore presets_;
    ParamReader reader_;
    AirEngine engine_;
    double fs_ = 48000.0;
    std::atomic<float> uiScale_{ 1.0f };
};

} // namespace powiew
