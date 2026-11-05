#pragma once
// REAPER lets a VST3 plugin ask about its own track. WERS sits before the track fader, so it hears
// the signal without that fader. Reading the fader is what keeps the word scores true to the mix.
#include <juce_audio_processors/juce_audio_processors.h>

namespace Steinberg { class FUnknown; }

namespace wers {

class ReaperBridge final : public juce::VST3ClientExtensions {
public:
    ReaperBridge() = default;
    ~ReaperBridge() override;
    void setIHostApplication(Steinberg::FUnknown* host) override;

    bool connected() const noexcept { return host_ != nullptr; }
    // Fader of this track and of the folders it passes through, in dB. Zero outside REAPER.
    float trackGainDb() const noexcept;
    // Tells open projects apart. Null outside REAPER.
    const void* project() const noexcept;
    // Moves the edit cursor a moment before this time, also during playback. False outside REAPER.
    bool canSeek() const noexcept { return setCursor_ != nullptr; }
    bool seek(double seconds) const noexcept;
private:
    using GetValue = double (*)(void* track, const char* name);
    using GetParent = void* (*)(void* track);
    using Validate = bool (*)(void* project, void* pointer, const char* typeName);
    using SetCursor = void (*)(void* project, double seconds, bool moveView, bool seekPlay);
    void* parentTrack() const noexcept;

    void* host_ = nullptr; // holds a reference, released in the destructor
    GetValue getValue_ = nullptr;
    GetParent getParent_ = nullptr;
    Validate validate_ = nullptr;
    SetCursor setCursor_ = nullptr;
    JUCE_DECLARE_NON_COPYABLE(ReaperBridge)
};

} // namespace wers
