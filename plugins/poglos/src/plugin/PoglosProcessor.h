#pragma once
#include "FactoryPresets.h"
#include "Params.h"

namespace pog {

class PoglosProcessor final : public juce::AudioProcessor {
public:
    static constexpr int kStateVersion = 1;

    PoglosProcessor();
    ~PoglosProcessor() override = default;

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
    const juce::String getName() const override { return juce::String::fromUTF8("WERS Pogłos"); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    // VST3 hosts list the one program by this name; an empty one fails Steinberg's validator.
    const juce::String getProgramName(int) override { return juce::String::fromUTF8("WERS Pogłos"); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }
    wers::ui::PresetStore& presets() noexcept { return presets_; }
    const Reverb& reverb() const noexcept { return reverb_; }
    const ParamReader& reader() const noexcept { return reader_; }
    double sampleRate() const noexcept { return fs_; }
    // The tempo the last block played at, for showing a synced predelay.
    double hostBpm() const noexcept { return bpm_.load(); }
    // The parameters as the engine hears them now.
    Params currentParams() const noexcept { return reader_.read(bpm_.load()); }

    float uiScale() const noexcept { return uiScale_.load(); }
    void setUiScale(float s) noexcept { uiScale_.store(s); }
private:
    void processAudio(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    wers::ui::PresetStore presets_;
    ParamReader reader_;
    Reverb reverb_;
    double fs_ = 48000.0;
    juce::AudioBuffer<float> dry_;
    float bypassMix_ = 0.0f, bypassStep_ = 0.001f;
    std::atomic<double> bpm_{ 120.0 };
    std::atomic<float> uiScale_{ 1.0f };
};

} // namespace pog
