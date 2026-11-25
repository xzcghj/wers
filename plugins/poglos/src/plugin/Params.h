#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Reverb.h"

// These identifiers are saved in projects and never change.
namespace pog::ids {
inline constexpr const char* algo = "algo";
inline constexpr const char* era = "era";
inline constexpr const char* mix = "mix";
inline constexpr const char* predelay = "predelay";
inline constexpr const char* preSync = "preSync";
inline constexpr const char* preNote = "preNote";
inline constexpr const char* decay = "decay";
inline constexpr const char* size = "size";
inline constexpr const char* attack = "attack";
inline constexpr const char* balance = "balance";
inline constexpr const char* diffEarly = "diffEarly";
inline constexpr const char* diffLate = "diffLate";
inline constexpr const char* bassMult = "bassMult";
inline constexpr const char* bassHz = "bassHz";
inline constexpr const char* damp = "damp";
inline constexpr const char* highCut = "highCut";
inline constexpr const char* lowCut = "lowCut";
inline constexpr const char* modRate = "modRate";
inline constexpr const char* modDepth = "modDepth";
inline constexpr const char* width = "width";
inline constexpr const char* freeze = "freeze";
inline constexpr const char* duck = "duck";
inline constexpr const char* shimmer = "shimmer";
inline constexpr const char* shimPitch = "shimPitch";
inline constexpr const char* bypass = "bypass";
} // namespace pog::ids

namespace pog {

inline juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Names shown for the choices; the order is saved in projects.
juce::StringArray algoNames();
juce::StringArray eraNames();
juce::StringArray noteNames();
juce::StringArray pitchNames();
// A note value in quarter notes, and the shimmer interval in semitones, for a choice index.
double noteQuarters(int index);
float pitchSemis(int index);

// Reads every parameter in plain units, without locking or allocating.
class ParamReader {
public:
    explicit ParamReader(juce::AudioProcessorValueTreeState& state);
    // bpm is the host tempo, used when predelay follows it.
    Params read(double bpm) const noexcept;
    bool bypassed() const noexcept { return bypass_->load() > 0.5f; }
    bool synced() const noexcept { return preSync_->load() > 0.5f; }
    int note() const noexcept { return juce::roundToInt(preNote_->load()); }
private:
    std::atomic<float>*algo_, *era_, *mix_, *predelay_, *preSync_, *preNote_, *decay_, *size_, *attack_, *balance_, *diffEarly_, *diffLate_,
        *bassMult_, *bassHz_, *damp_, *highCut_, *lowCut_, *modRate_, *modDepth_, *width_, *freeze_, *duck_, *shimmer_, *shimPitch_, *bypass_;
};

} // namespace pog
