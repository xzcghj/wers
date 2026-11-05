#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// These identifiers are saved in projects and never change.
namespace barwa::ids {
inline constexpr const char* bands = "bands";
inline constexpr const char* xover1 = "xover1";
inline constexpr const char* xover2 = "xover2";
inline constexpr const char* char1 = "char1";
inline constexpr const char* char2 = "char2";
inline constexpr const char* char3 = "char3";
inline constexpr const char* drive1 = "drive1";
inline constexpr const char* drive2 = "drive2";
inline constexpr const char* drive3 = "drive3";
inline constexpr const char* bandMix1 = "bandMix1";
inline constexpr const char* bandMix2 = "bandMix2";
inline constexpr const char* bandMix3 = "bandMix3";
inline constexpr const char* solo1 = "solo1";
inline constexpr const char* solo2 = "solo2";
inline constexpr const char* solo3 = "solo3";
inline constexpr const char* mask = "mask";
inline constexpr const char* tone = "tone";
inline constexpr const char* mix = "mix";
inline constexpr const char* autoLevel = "autoLevel";
inline constexpr const char* output = "output";
inline constexpr const char* delta = "delta";
inline constexpr const char* quality = "quality";
inline constexpr const char* bypass = "bypass";
} // namespace barwa::ids

namespace barwa {
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace barwa
