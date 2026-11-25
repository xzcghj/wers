#pragma once
#include "PresetStore.h"

namespace pog {

std::vector<wers::ui::FactoryPreset> factoryPresets();
// What presets never touch: bypass and freeze are things you do, not part of a sound.
juce::StringArray presetExcludedIds();

} // namespace pog
