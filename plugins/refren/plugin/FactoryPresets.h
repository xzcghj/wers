#pragma once
#include "PresetStore.h"

namespace refren {

struct PresetGroup {
    const char* name;
    int atLeast;
};

inline constexpr std::array kGroups{
    PresetGroup{ "Dubel", 12 },
};

std::vector<wers::ui::FactoryPreset> factoryPresets();
juce::StringArray presetExcludedIds();

} // namespace refren
