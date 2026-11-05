#pragma once
#include "Theme.h"
#include "analysis/Types.h"

namespace wers::ui {

// The colour of a word score: coral when it dies, amber when it fades, mint when it is clear.
inline juce::Colour scoreColour(float score) {
    return score < kScoreDies ? colour::dies : (score < kScoreFades ? colour::weak : colour::accent);
}

} // namespace wers::ui
