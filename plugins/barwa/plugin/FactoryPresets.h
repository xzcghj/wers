#pragma once
#include "PresetStore.h"

namespace barwa {

std::vector<wers::ui::FactoryPreset> factoryPresets();
juce::StringArray presetExcludedIds();

} // namespace barwa
