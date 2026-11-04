#pragma once
#include "Sii.h"

namespace wers {

struct PlannerLimits {
    float maxBoostDb = 4.0f;
    float maxCutDb = 6.0f;
};

// Finds the smallest set of vocal boosts and beat cuts that brings a word to the target score on a
// device, using the score model itself. Returns the fix; the word is not modified.
WordFix planFix(const BandFrames& f, const Word& word, Device target, const MixState& mix, const PlannerLimits& limits = {});

// Recomputes all device verdicts of a word.
void scoreAllDevices(const BandFrames& f, Word& word, const MixState& mix);

} // namespace wers
