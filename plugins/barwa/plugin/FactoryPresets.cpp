#include "FactoryPresets.h"
#include "Params.h"

namespace barwa {

// Starting points, set from measurements so Auto level holds the loudness. Listening rounds tune them.
// Characters: 0 Lampa, 1 Taśma, 2 Transformator, 3 Tranzystor. Band count: 0, 1, 2 for one, two, three.
std::vector<wers::ui::FactoryPreset> factoryPresets() {
    using namespace ids;
    return {
        { "Czysty start", {} },
        { "Ciepły refren", { { drive1, 9.0f }, { tone, -15.0f } } },
        { "Gęsty rap", { { bands, 1.0f }, { xover1, 350.0f }, { char1, 0.0f }, { drive1, 8.0f }, { char2, 3.0f }, { drive2, 10.0f }, { mix, 85.0f } } },
        { "Szept z bliska", { { char1, 1.0f }, { drive1, 6.0f }, { mask, 40.0f }, { tone, 10.0f } } },
        { "Przód miksu", { { bands, 2.0f }, { xover1, 250.0f }, { xover2, 2500.0f }, { char2, 0.0f }, { drive2, 8.0f }, { char3, 1.0f }, { drive3, 6.0f }, { tone, 15.0f } } },
        { "Rage", { { char1, 3.0f }, { drive1, 22.0f }, { mask, 70.0f }, { tone, 10.0f }, { mix, 90.0f } } },
        { "Dubel w tle", { { char1, 1.0f }, { drive1, 14.0f }, { tone, -35.0f } } },
        { "Klej na bit", { { char1, 1.0f }, { drive1, 5.0f }, { mask, 0.0f }, { mix, 70.0f } } },
        { "808 na telefon", { { bands, 1.0f }, { xover1, 150.0f }, { char1, 2.0f }, { drive1, 16.0f }, { mask, 0.0f }, { tone, 20.0f } } },
        { "Werbel z pazurem", { { char1, 3.0f }, { drive1, 12.0f }, { mask, 0.0f }, { mix, 60.0f }, { tone, 10.0f } } },
        { "Hi-haty bez ostrości", { { char1, 1.0f }, { drive1, 8.0f }, { mask, 0.0f }, { mix, 80.0f }, { tone, -50.0f } } },
        { "Delikatny klej na master", { { drive1, 3.0f }, { mask, 0.0f }, { mix, 50.0f } } },
    };
}

juce::StringArray presetExcludedIds() {
    return { ids::quality, ids::bypass, ids::delta, ids::solo1, ids::solo2, ids::solo3 };
}

} // namespace barwa
