#pragma once
#include "PresetStore.h"

namespace refren {

struct PresetGroup {
    const char* name;
    int atLeast;
};

inline constexpr std::array kGroups{
    PresetGroup{ "Dubel", 12 },
    PresetGroup{ "Refren", 12 },
    PresetGroup{ "Ekipa", 10 },
    PresetGroup{ "Chór", 10 },
    PresetGroup{ "Głęboki", 10 },
    PresetGroup{ "Wysoki", 10 },
    PresetGroup{ "Harmonie", 10 },
    PresetGroup{ "Ad-liby", 12 },
    PresetGroup{ "Szept", 8 },
    PresetGroup{ "Przester", 10 },
    PresetGroup{ "Efekty", 12 },
    PresetGroup{ "Instrumenty", 8 },
};

std::vector<wers::ui::FactoryPreset> factoryPresets();
juce::StringArray presetExcludedIds();

} // namespace refren
