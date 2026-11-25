#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Lampa.h"

// These identifiers are saved in projects and never change.
namespace lampa::ids {
inline constexpr const char* squeeze = "squeeze";
inline constexpr const char* gain = "gain";
inline constexpr const char* color = "color";
inline constexpr const char* mix = "mix";
inline constexpr const char* mode = "mode";
inline constexpr const char* limit = "limit";
inline constexpr const char* link = "link";
inline constexpr const char* sidechain = "sidechain";
inline constexpr const char* output = "output";
inline constexpr const char* bypass = "bypass";
} // namespace lampa::ids

namespace lampa {

inline juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Reads every parameter in plain units, without locking or allocating.
class ParamReader {
public:
    explicit ParamReader(juce::AudioProcessorValueTreeState& state);
    Params read() const noexcept;
    bool bypassed() const noexcept { return bypass_->load() > 0.5f; }
private:
    std::atomic<float>*squeeze_, *gain_, *color_, *mix_, *mode_, *limit_, *link_, *sidechain_, *output_, *bypass_;
};

} // namespace lampa
