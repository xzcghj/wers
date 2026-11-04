#pragma once
#include "ModelManager.h"
#include "Params.h"
#include "ProcessRecognizer.h"
#include "ReaperBridge.h"
#include "analysis/SongSession.h"
#include "dsp/Fonem.h"
#include "dsp/Monitor.h"
#include "dsp/Place.h"
#include "dsp/Space.h"
#include "dsp/VocalFix.h"
#include "link/GroupControl.h"

namespace wers {

class WersProcessor final : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::Timer {
public:
    WersProcessor();
    ~WersProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "WERS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;
    void updateTrackProperties(const TrackProperties& properties) override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &reaper_; }

    // Interface for the editor. Message thread.
    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }
    Role role() const noexcept { return static_cast<Role>(activeRole_.load()); }
    SongSession& session() noexcept { return session_; }
    ModelManager& model() noexcept { return *model_; }
    bool canRecognise() const;
    LinkGroup* group() const noexcept { return group_; }
    int linkSlot() const noexcept { return slot_; }
    bool isStale() const noexcept { return staleFlag_.load(); }
    bool isTransportPlaying() const noexcept { return playing_.load(); }
    double currentSeconds() const noexcept { return seconds_.load(); }
    double hostRate() const noexcept { return fs_; }
    bool sidechainActive() const noexcept { return sideSeen_.load(); }
    void beatCuts(float* out) const noexcept { for (size_t i = 0; i < kCutBands; ++i) out[i] = shownCuts_[i].load(std::memory_order_relaxed); }
    void classLevels(float& vowel, float& sib, float& stop, float& breath) const noexcept;
    float vowelReductionDb() const noexcept { return shownGr_.load(); }
    float deEssDb() const noexcept { return shownDeEss_.load(); }
    struct MeterValues { double momentary, shortTerm, integrated, truePeak, seconds, gainDb; };
    MeterValues meters() const noexcept;
    void resetMeters() noexcept { resetMeterRequest_.store(true); }
    void applySuggestion(const SuggestedSettings& s);
    void applyCharacterPreset(float x, float y, float density, float clarity, float space);
    float uiScale() const noexcept { return uiScale_.load(); }
    void setUiScale(float s) noexcept { uiScale_.store(s); }
    bool startAnalysis();
    void chooseRole(Role r);
    void cycleGroup();
    int groupNumber() const noexcept { return appliedGroup_ + 1; }
private:
    void parameterChanged(const juce::String& id, float value) override;
    void timerCallback() override;
    void applyRoleAndGroup();
    void processChunk(float* const* ch, int nc, const float* const* sc, int nsc, int n, int64_t timeline, bool playing, double bpm, bool bypassed);
    void processWet(float* const* ch, int nc, const float* const* sc, int nsc, int n, int64_t timeline, bool playing, double bpm, Role r);
    void setParam(const char* id, float plainValue);

    juce::AudioProcessorValueTreeState apvts_;
    std::atomic<float>* pDensity_, *pClarity_, *pAttack_, *pBreath_, *pPlace_, *pSpace_, *pCharX_, *pCharY_, *pFixes_, *pMonDevice_, *pMonNorm_, *pBypass_;

    double fs_ = 48000.0;
    static constexpr int kChunk = 1024;

    Fonem fonem_;
    VocalFix vocalFix_;
    Space space_;
    Place place_;
    Monitor monitor_;
    BandEnvelope liveEnv_;
    StaleDetector staleDetector_;
    int ringHop_ = 240, ringCount_ = 0;

    ReaperBridge reaper_;
    std::shared_ptr<LinkHub> hub_;
    LinkGroup* group_ = nullptr;
    GroupPlaceControl control_;
    std::atomic<LinkGroup*> rtGroup_{ nullptr };
    std::atomic<int> rtSlot_{ -1 };
    std::atomic<const GroupPlaceControl*> rtControl_{ nullptr };
    int slot_ = -1, appliedGroup_ = -1;
    const void* appliedProject_ = nullptr;
    std::shared_ptr<TimelineStore> vocalStore_, sideStore_, beatStore_;
    bool beatStoreRegistered_ = false;
    juce::SharedResourcePointer<ModelManager> model_;     // before the session, whose worker thread uses it
    SongSession session_;

    std::atomic<int> activeRole_{ 0 };
    std::atomic<int> roleForAudio_{ 0 };
    std::atomic<bool> needsReset_{ false };
    std::atomic<bool> resetMeterRequest_{ false };
    std::atomic<bool> roleDirty_{ true };
    bool roleChosenByUser_ = false;

    std::atomic<bool> playing_{ false }, staleFlag_{ false }, sideSeen_{ false };
    std::atomic<double> seconds_{ 0.0 };
    std::atomic<juce::uint32> lastBlockMs_{ 0 };
    std::array<std::atomic<float>, kCutBands> shownCuts_{};
    std::atomic<float> shownVowel_{ 0 }, shownSib_{ 0 }, shownStop_{ 0 }, shownBreath_{ 0 }, shownGr_{ 0 }, shownDeEss_{ 0 };
    std::atomic<double> shownM_{ -140 }, shownS_{ -140 }, shownI_{ -140 }, shownTp_{ -140 }, shownSec_{ 0 }, shownGain_{ 0 };
    std::atomic<float> uiScale_{ 1.0f };

    // Bypass crossfade with a matching delay, so toggling never clicks or shifts the audio in time.
    juce::AudioBuffer<float> dryScratch_;
    std::array<DelayLine, 2> bypassDelay_{};
    float bypassMix_ = 0.0f, bypassStep_ = 0.001f;

    int stoppedTicks_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WersProcessor)
};

} // namespace wers
