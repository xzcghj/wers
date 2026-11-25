#pragma once
// The gain cell: how much the level is turned down for a given input, and how fast that follows.
#include <algorithm>
#include <cmath>

namespace lampa {

// Gain reduction in dB for a level above the threshold. Two soft knees in a row: the first one starts the
// compression, the second one, a few dB higher, adds more, so the curve gets harder the more it reduces.
struct Curve {
    float knee1 = 14.0f, slope1 = 0.45f; // slope = 1 - 1 / ratio
    float second = 10.0f, knee2 = 14.0f, slope2 = 0.25f;

    static float knee(float over, float width, float slope) {
        const float half = 0.5f * width;
        if (over <= -half) return 0.0f;
        if (over >= half) return slope * over;
        const float t = over + half;
        return slope * t * t / (2.0f * width);
    }

    float reduction(float over) const { return knee(over, knee1, slope1) + knee(over - second, knee2, slope2); }

    static Curve mix(const Curve& a, const Curve& b, float t) {
        auto l = [t](float x, float y) { return x + t * (y - x); };
        return { l(a.knee1, b.knee1), l(a.slope1, b.slope1), l(a.second, b.second), l(a.knee2, b.knee2), l(a.slope2, b.slope2) };
    }
};

// The four characters: optical and tube, each leveling or limiting.
inline const Curve kOptoCurve{ 14.0f, 0.45f, 10.0f, 14.0f, 0.25f };  // 2:1 rising to about 3,3:1
inline const Curve kOptoLimit{ 8.0f, 0.75f, 5.0f, 10.0f, 0.20f };    // 4:1 rising to 20:1
inline const Curve kTubeCurve{ 24.0f, 0.35f, 12.0f, 20.0f, 0.35f };  // a very wide knee up to about 3,3:1
inline const Curve kTubeLimit{ 12.0f, 0.60f, 6.0f, 14.0f, 0.30f };   // 2,5:1 rising to 10:1

// Timing of one character, in seconds unless said otherwise.
struct Timing {
    float attack;       // for a small step; bigger steps are caught faster
    float release;      // the fast part of the release
    float memory;       // the slow tail, before it grows
    float charge;       // how long it takes the tail to take over part of the reduction
    float share;        // how much of a long reduction the tail can hold, 0..1
    float growth;       // how much longer the tail gets after long, hard compression (times)
    float detector;     // averaging of the level detector

    static Timing mix(const Timing& a, const Timing& b, float t) {
        // Times blend on a log scale, so halfway between 3 ms and 10 ms is about 5,5 ms.
        auto g = [t](float x, float y) { return x * std::pow(y / x, t); };
        auto l = [t](float x, float y) { return x + t * (y - x); };
        return { g(a.attack, b.attack), g(a.release, b.release), g(a.memory, b.memory), g(a.charge, b.charge),
                 l(a.share, b.share), l(a.growth, b.growth), g(a.detector, b.detector) };
    }
};

// Optical: a lamp lights a photocell. Slower attack, a release that drops quickly at first and then lingers,
// and the longer and harder it worked, the longer it lingers.
inline const Timing kOptoTiming{ 0.010f, 0.060f, 0.60f, 1.2f, 0.55f, 4.0f, 0.006f };
// Tube: the bias of the tubes moves the gain. Quicker on attack, one smooth and slower release, little memory.
inline const Timing kTubeTiming{ 0.0025f, 0.30f, 1.4f, 2.5f, 0.35f, 1.5f, 0.004f };

// Per-sample coefficients for the cell, worked out from a Timing every few samples.
struct CellCoefs {
    float attackX = 0.0f;   // 1 / (attack * fs)
    float release = 0.0f;   // multiplier of the fast part, per sample
    float memoryX = 0.0f;   // 1 / (memory * fs)
    float charge = 0.0f;    // share moved into the tail, per sample
    float share = 0.5f, growth = 1.0f;
    float heatUp = 0.0f, heatDown = 0.0f;

    static CellCoefs from(const Timing& t, double fs) {
        CellCoefs c;
        c.attackX = static_cast<float>(1.0 / (t.attack * fs));
        c.release = static_cast<float>(std::exp(-1.0 / (t.release * fs)));
        c.memoryX = static_cast<float>(1.0 / (t.memory * fs));
        c.charge = static_cast<float>(1.0 - std::exp(-1.0 / (t.charge * fs)));
        c.share = t.share;
        c.growth = t.growth;
        c.heatUp = static_cast<float>(1.0 - std::exp(-1.0 / (4.0 * fs)));
        c.heatDown = static_cast<float>(1.0 - std::exp(-1.0 / (10.0 * fs)));
        return c;
    }
};

// The reduction is held in two parts. The fast part follows the music; part of it slowly moves into the
// slow part, the memory, while the cell keeps working. On release the fast part falls away in tens of
// milliseconds and the memory fades over seconds, and a hot cell (heat) fades even slower.
struct Cell {
    float fast = 0.0f, slow = 0.0f, heat = 0.0f;

    void reset() { fast = slow = heat = 0.0f; }
    float reduction() const { return fast + slow; }

    float step(float target, const CellCoefs& k) {
        float g = fast + slow;
        if (target > g) {
            // A bigger step charges the cell faster, like a lamp driven harder.
            const float x = std::min(1.0f, k.attackX * (1.0f + (target - g) * (1.0f / 6.0f)));
            fast += (target - g) * (x / (1.0f + 0.5f * x));
        } else {
            const float floor = std::max(0.0f, target - slow);
            fast = floor + (fast - floor) * k.release;
            if (target < slow) {
                const float x = k.memoryX / (1.0f + k.growth * heat);
                slow = target + (slow - target) * (1.0f - x / (1.0f + 0.5f * x));
            }
        }
        g = fast + slow;
        // Deeper reduction lets the memory hold a bigger share.
        const float want = g * std::min(0.9f, k.share + 0.2f * std::min(1.0f, g * (1.0f / 18.0f)));
        if (want > slow) {
            const float moved = (want - slow) * k.charge;
            slow += moved;
            fast -= moved;
        }
        const float drive = std::min(1.0f, g * (1.0f / 12.0f));
        heat += (drive - heat) * (drive > heat ? k.heatUp : k.heatDown);
        if (fast < 1e-7f) fast = 0.0f;
        if (slow < 1e-7f) slow = 0.0f;
        return fast + slow;
    }
};

} // namespace lampa
