#pragma once
#include "PresetStore.h"

namespace doc {

std::vector<wers::ui::FactoryPreset> factoryPresets();
// What presets never touch: switches for checking and comparing, not part of a sound.
juce::StringArray presetExcludedIds();

} // namespace doc
