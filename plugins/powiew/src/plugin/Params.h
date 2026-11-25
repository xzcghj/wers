#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AirEngine.h"

// These identifiers are saved in projects and never change.
namespace powiew::ids {
inline constexpr const char* presence = "presence";
inline constexpr const char* air = "air";
inline constexpr const char* presenceHz = "presenceHz";
inline constexpr const char* airHz = "airHz";
inline constexpr const char* smooth = "smooth";
inline constexpr const char* character = "character";
inline constexpr const char* mix = "mix";
inline constexpr const char* trim = "trim";
inline constexpr const char* monoSafe = "monoSafe";
inline constexpr const char* delta = "delta";
inline constexpr const char* bypass = "bypass";
} // namespace powiew::ids

namespace powiew {

inline juce::String pl(const char* utf8) { return juce::String::fromUTF8(utf8); }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Reads every parameter in plain units, without locking or allocating.
class ParamReader {
public:
    explicit ParamReader(juce::AudioProcessorValueTreeState& state);
    Params read() const noexcept;
    bool bypassed() const noexcept { return bypass_->load() > 0.5f; }
private:
    std::atomic<float>*presence_, *air_, *presenceHz_, *airHz_, *smooth_, *character_, *mix_, *trim_, *monoSafe_, *delta_, *bypass_;
};

} // namespace powiew
