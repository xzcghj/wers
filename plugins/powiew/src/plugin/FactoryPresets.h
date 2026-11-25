#pragma once
#include "PresetStore.h"

namespace powiew {

std::vector<wers::ui::FactoryPreset> factoryPresets();
// What presets never touch: switches for listening and comparing, not part of a sound.
juce::StringArray presetExcludedIds();

} // namespace powiew
