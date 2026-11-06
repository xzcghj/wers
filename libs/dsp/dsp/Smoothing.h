#pragma once
// Values that change without a click.
#include "Common.h"

namespace wers {

// A value that changes in a straight line between two control points.
struct Ramp {
    float now = 0.0f, end = 0.0f, step = 0.0f;
    void set(float value) noexcept { now = end = value; step = 0.0f; }
    void aim(float next, float perSample) noexcept { now = end; end = next; step = (next - now) * perSample; }
};

// Closes before a change that cannot be smoothed, waits for the filters to settle and opens again.
struct Gate {
    enum class Phase { Open, Closing, Settling, Opening };
    Phase phase = Phase::Open;
    float level = 1.0f;
    int wait = 0;
    // One control step. True at the one moment when the change is safe, because nothing is heard.
    bool advance(bool changeWanted, float step, int settleTicks) noexcept {
        switch (phase) {
            case Phase::Open:
                if (changeWanted) phase = Phase::Closing;
                break;
            case Phase::Closing:
                level -= step;
                if (level <= 0.0f) {
                    level = 0.0f;
                    wait = settleTicks;
                    phase = Phase::Settling;
                }
                break;
            case Phase::Settling: {
                // The fade reaches zero one control step after the level does, so this is when nothing is heard.
                const bool closedJustNow = wait == settleTicks;
                if (--wait <= 0) phase = Phase::Opening;
                return closedJustNow;
            }
            case Phase::Opening:
                if (changeWanted) {
                    phase = Phase::Closing;
                    break;
                }
                level += step;
                if (level >= 1.0f) openNow();
                break;
        }
        return false;
    }
    void openNow() noexcept { phase = Phase::Open; level = 1.0f; wait = 0; }
};

} // namespace wers
