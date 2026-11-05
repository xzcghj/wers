// Logs each click it hears with the project time the host reported for that sample.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace {
std::atomic<int> gNextId{ 0 };

class Probe final : public juce::AudioProcessor, private juce::Timer {
public:
    Probe() : AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::stereo(), true).withOutput("Out", juce::AudioChannelSet::stereo(), true)),
              id_(++gNextId) {
        log_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getSiblingFile("Projects").getChildFile("WERS").getChildFile("logs").getChildFile("probe_" + juce::String(id_) + ".txt");
        log_.getParentDirectory().createDirectory();
        log_.replaceWithText("probe " + juce::String(id_) + "\n");
        startTimerHz(5);
    }
    ~Probe() override { stopTimer(); timerCallback(); }
    void prepareToPlay(double sr, int block) override { rate_.store(sr); log_.appendText("prepare " + juce::String(sr) + " block " + juce::String(block) + "\n"); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::int64 at = -1; bool playing = false;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { if (auto t = pos->getTimeInSamples()) at = *t; playing = pos->getIsPlaying(); }
        const auto* x = b.getReadPointer(0);
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const float v = std::abs(x[i]);
            if (armed_ && v > 0.3f) {
                armed_ = false; quiet_ = 0;
                const int w = write_.load();
                events_[static_cast<size_t>(w % 64)] = { at >= 0 ? at + i : -1, playing };
                write_.store(w + 1);
            } else if (!armed_) {
                quiet_ = v < 0.01f ? quiet_ + 1 : 0;
                if (quiet_ > 2400) armed_ = true;
            }
        }
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "WERS Probe"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void updateTrackProperties(const TrackProperties& p) override { if (p.name.has_value()) log_.appendText("track " + *p.name + "\n"); }
private:
    void timerCallback() override {
        const int w = write_.load();
        while (read_ < w) {
            const auto& e = events_[static_cast<size_t>(read_ % 64)];
            log_.appendText("click at reported sample " + juce::String(e.sample) + (e.playing ? " playing" : " stopped") + "\n");
            ++read_;
        }
    }
    struct Event { juce::int64 sample; bool playing; };
    int id_;
    juce::File log_;
    std::array<Event, 64> events_{};
    std::atomic<int> write_{ 0 };
    int read_ = 0;
    bool armed_ = true;
    int quiet_ = 0;
    std::atomic<double> rate_{ 48000.0 };
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Probe(); }
