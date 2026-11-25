#pragma once
#include "PresetStore.h"

namespace lampa {

std::vector<wers::ui::FactoryPreset> factoryPresets();
// What presets never touch: bypass is for comparing, not part of a sound.
juce::StringArray presetExcludedIds();

} // namespace lampa
