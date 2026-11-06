#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// These identifiers are saved in projects and never change.
namespace refren::ids {
inline constexpr const char* amount = "amount";
inline constexpr const char* width = "width";
inline constexpr const char* time = "time";
inline constexpr const char* tilt = "tilt";
inline constexpr const char* grain = "grain";
inline constexpr const char* grainChar = "grainChar";
inline constexpr const char* motion = "motion";
inline constexpr const char* motionRate = "motionRate";
inline constexpr const char* motionDepth = "motionDepth";
inline constexpr const char* echoMix = "echoMix";
inline constexpr const char* echoTime = "echoTime";
inline constexpr const char* echoFeedback = "echoFeedback";
inline constexpr const char* echoPing = "echoPing";
inline constexpr const char* echoTone = "echoTone";
inline constexpr const char* verbMix = "verbMix";
inline constexpr const char* verbSize = "verbSize";
inline constexpr const char* verbDecay = "verbDecay";
inline constexpr const char* verbTone = "verbTone";
inline constexpr const char* verbPre = "verbPre";
inline constexpr const char* verbShimmer = "verbShimmer";
inline constexpr const char* duck = "duck";
inline constexpr const char* sendDry = "sendDry";
inline constexpr const char* consonants = "consonants";
inline constexpr const char* dry = "dry";
inline constexpr const char* trim = "trim";
inline constexpr const char* output = "output";
inline constexpr const char* delta = "delta";
inline constexpr const char* bypass = "bypass";

// What follows the number of the layer: l1On, l2Source and so on.
inline constexpr const char* on = "On";
inline constexpr const char* source = "Source";
inline constexpr const char* voices = "Voices";
inline constexpr const char* semis = "Semis";
inline constexpr const char* formant = "Formant";
inline constexpr const char* level = "Level";
inline constexpr const char* layerWidth = "Width";
inline constexpr const char* layerTime = "Time";
inline constexpr const char* detune = "Detune";
inline constexpr const char* low = "Low";
inline constexpr const char* high = "High";
inline constexpr const char* note = "Note";
} // namespace refren::ids

namespace refren {
inline constexpr int kLayers = 4;
// The layer counts from 0 here and from 1 in the identifier, so the second layer's source is layerId(1, ids::source).
juce::String layerId(int layer, const char* suffix);
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace refren
