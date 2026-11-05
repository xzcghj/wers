#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace wers::ids {
inline constexpr const char* role = "role";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* density = "density";
inline constexpr const char* clarity = "clarity";
inline constexpr const char* attack = "attack";
inline constexpr const char* breath = "breath";
inline constexpr const char* place = "place";
inline constexpr const char* space = "space";
inline constexpr const char* charX = "charX";
inline constexpr const char* charY = "charY";
inline constexpr const char* fixes = "fixes";
inline constexpr const char* viewDevice = "viewDevice";
inline constexpr const char* monDevice = "monDevice";
inline constexpr const char* monNorm = "monNorm";
inline constexpr const char* group = "group";
} // namespace wers::ids

namespace wers {
enum class Role : int { Vocal = 0, Beat = 1, Master = 2 };
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace wers
