#pragma once
// Finds and downloads the speech model. Nothing here runs on the audio thread.
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace wers {

class ModelManager : private juce::URL::DownloadTaskListener {
public:
    enum class State : int { Missing = 0, Downloading, Ready, Failed };
    static juce::File modelFile();
    static juce::int64 expectedBytes();

    ModelManager();
    ~ModelManager() override;
    State state() const noexcept { return state_.load(); }
    float progress() const noexcept { return progress_.load(); }
    void startDownload();
    void cancelDownload();
    // Lets tests use a local address.
    void setSourceForTests(const juce::URL& url, juce::int64 size) { source_ = url; expected_ = size; }
private:
    void finished(juce::URL::DownloadTask*, bool success) override;
    void progress(juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) override;
    juce::URL source_;
    juce::int64 expected_;
    std::atomic<State> state_{ State::Missing };
    std::atomic<float> progress_{ 0.0f };
    std::unique_ptr<juce::URL::DownloadTask> task_;
    juce::CriticalSection lock_;
};

} // namespace wers
