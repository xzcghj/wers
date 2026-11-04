#pragma once
// The analysis of one song, owned by a vocal instance. All work runs on one background thread; the
// interface can be called from any non-realtime thread and results are immutable snapshots.
#include "FixPlanner.h"
#include "Frames.h"
#include "Recognizer.h"
#include "link/LinkHub.h"
#include <condition_variable>
#include <deque>
#include <thread>

namespace wers {

struct SongResult {
    std::vector<Word> words;
    bool hasText = false;        // words come from recognition, not from voice fragments
    bool hasBeat = false;        // a beat signal was heard
    bool textFailed = false;     // recognition was tried and could not finish
    double endSeconds = 0.0;
    double lufs = -14.0;
};

enum class SessionPhase : int { Idle = 0, Listening, Measuring, Recognizing, CheckingMix, Scoring, Ready, Failed };

// What "Dopasuj do bitu" suggests.
struct SuggestedSettings { float density, clarity, attack, breath, place, space, characterX, characterY; bool valid = false; };

class SongSession {
public:
    SongSession();
    ~SongSession();

    // Wiring. stores and slot must stay valid until the session is destroyed or re-attached.
    void attachLink(LinkGroup* group, int slot) noexcept;
    void setStores(std::shared_ptr<TimelineStore> vocal, std::shared_ptr<TimelineStore> sidechain);
    void setRecognizerFactory(std::function<std::unique_ptr<Recognizer>()> factory);
    void setLyrics(const std::string& lyrics);
    std::string lyrics() const;
    void setLanguage(const std::string& language);
    void setPlaceDepth(float depth);
    // Fader positions after the plugins, when the host tells them. Scores follow them right away.
    void setFaders(float vocalDb, float beatDb);   // rescoring happens in the background

    // Commands.
    void startListening();
    void stopListeningAndAnalyse();    // no-op when not listening
    void cancelListening();
    void fixWord(int index, Device target);
    void undoFix(int index);
    void fixAllBelow(float threshold, Device target);
    void clearAllFixes();
    void analyseAgain();               // re-runs everything on the audio still in memory
    bool canAnalyseAgain() const;
    // Works out the fix for a word without applying it, so the interface can show it first.
    void requestPreview(int index, Device target);
    struct Preview { int index = -1; Device target = Device::Phone; WordFix fix; float predictedScore = 0.0f; };
    Preview preview() const;

    // State.
    SessionPhase phase() const noexcept { return phase_.load(std::memory_order_relaxed); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    bool busy() const noexcept { return busy_.load(std::memory_order_relaxed); }
    uint64_t generation() const noexcept { return generation_.load(std::memory_order_relaxed); }
    std::shared_ptr<const SongResult> result() const;
    double heardSeconds() const;
    SuggestedSettings suggestSettings() const;
    std::string failureCode() const;

    // Project state. load() returns false and leaves the session empty on damaged data.
    std::vector<uint8_t> save() const;
    bool load(const uint8_t* data, size_t size);

    // Test hook: blocks until the worker is idle.
    void waitIdle() const;
private:
    using Job = std::function<void()>;
    void enqueue(Job job);
    void workerLoop();
    void runAnalysis(std::vector<float> vocal, std::vector<float> beat, bool hadBeat);
    void rescoreAll();
    void publish(std::vector<Word> words, bool hasText, bool hasBeat, double end, double lufs, bool textFailed = false);
    void publishPlan(const std::vector<Word>& words);
    bool keepGoing() const noexcept { return !stop_.load() ; }

    mutable std::mutex stateMutex_;
    std::shared_ptr<const SongResult> result_;
    std::shared_ptr<const BandFrames> frames_;
    std::shared_ptr<TimelineStore> vocalStore_, sideStore_;
    std::function<std::unique_ptr<Recognizer>()> recognizerFactory_;
    std::string lyrics_, language_ = "pl", failure_;
    MixState mix_;
    LinkGroup* group_ = nullptr;
    int slot_ = -1;

    Preview preview_;
    std::atomic<bool> rescorePending_{ false };
    std::atomic<int> previewRequest_{ 0 };
    std::vector<float> lastVocal_, lastBeat_;
    bool lastHadBeat_ = false;

    std::atomic<SessionPhase> phase_{ SessionPhase::Idle };
    std::atomic<float> progress_{ 0.0f };
    std::atomic<bool> busy_{ false };
    std::atomic<uint64_t> generation_{ 0 };
    std::atomic<bool> stop_{ false };
    std::atomic<bool> cancelJob_{ false };

    mutable std::mutex queueMutex_;
    mutable std::condition_variable queueCv_, idleCv_;
    std::deque<Job> queue_;
    bool workerBusy_ = false;
    std::thread worker_;
};

} // namespace wers
