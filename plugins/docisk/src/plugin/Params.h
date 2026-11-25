#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Compressor.h"

// These identifiers are saved in projects and never change.
namespace doc::ids {
inline constexpr const char* threshold = "threshold";
inline constexpr const char* ratio = "ratio";
inline constexpr const char* knee = "knee";
inline constexpr const char* attack = "attack";
inline constexpr const char* release = "release";
inline constexpr const char* autoRelease = "autoRelease";
inline constexpr const char* range = "range";
inline constexpr const char* makeup = "makeup";
inline constexpr const char* autoMakeup = "autoMakeup";
inline constexpr const char* mix = "mix";
inline constexpr const char* output = "output";
inline constexpr const char* character = "character";
inline constexpr const char* detector = "detector";
inline constexpr const char* link = "link";
inline constexpr const char* lookahead = "lookahead";
inline constexpr const char* keyHighPass = "scHighPass";
inline constexpr const char* keyTilt = "scTilt";
inline constexpr const char* external = "scExternal";
inline constexpr const char* listen = "scListen";
inline constexpr const char* clip = "clip";
inline constexpr const char* bypass = "bypass";
} // namespace doc::ids

namespace doc {

inline juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// The ratio knob: the bottom 8% expands (1:0,5 to 1:1), the rest runs from 1:1 to infinity on a log scale.
float ratioFromNormalised(float n) noexcept;
float normalisedFromRatio(float r) noexcept;
juce::String ratioText(float r);

// Reads every parameter in plain units, without locking or allocating.
class ParamReader {
public:
    explicit ParamReader(juce::AudioProcessorValueTreeState& state);
    Params read() const noexcept;
    bool bypassed() const noexcept { return bypass_->load() > 0.5f; }
    float lookaheadMs() const noexcept { return lookahead_->load(); }
private:
    std::atomic<float>*threshold_, *ratio_, *knee_, *attack_, *release_, *autoRelease_, *range_, *makeup_, *autoMakeup_, *mix_, *output_,
        *character_, *detector_, *link_, *lookahead_, *keyHighPass_, *keyTilt_, *external_, *listen_, *clip_, *bypass_;
};

} // namespace doc
