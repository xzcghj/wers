#pragma once
// Immutable data the audio threads read: the word fixes and the vocal band map of a song.
#include "analysis/Types.h"

namespace wers {

struct FixEvent {
    double t0 = 0.0, t1 = 0.0;
    std::array<float, kCutBands> beatCutDb{};
    std::array<float, kBoostBands> vocalBoostDb{};
    std::vector<std::pair<float, float>> consonants;
};

class Plan {
public:
    std::vector<FixEvent> events;          // sorted by start time
    std::vector<uint8_t> vocalMap;         // frames * kCutBands, quantised band level
    std::vector<float> levelTrack;         // voice level of each frame, used to detect a changed vocal
    int mapFrames = 0;

    static uint8_t quantise(float db) noexcept { return static_cast<uint8_t>(std::clamp((db + 120.0f) * 2.0f, 0.0f, 255.0f)); }
    static float expand(uint8_t q) noexcept { return q * 0.5f - 120.0f; }

    // Vocal band level at time t. Looks 20 ms ahead, so a cut is already there when a sound starts.
    bool vocalBandsAt(double t, float* levelsDb) const noexcept {
        const int f = frameAtTime(t);
        if (f < 0 || f >= mapFrames) return false;
        const int g = std::min(f + 2, mapFrames - 1);
        const uint8_t* a = vocalMap.data() + static_cast<size_t>(f) * kCutBands;
        const uint8_t* b = vocalMap.data() + static_cast<size_t>(g) * kCutBands;
        for (int j = 0; j < kCutBands; ++j) levelsDb[j] = expand(std::max(a[j], b[j]));
        return true;
    }

    // Beat cut from word fixes at t, with a short ramp around each word. Positive dB.
    bool fixCutsAt(double t, float* cutsDb) const noexcept {
        if (events.empty()) return false;
        static constexpr double kPre = 0.015, kPost = 0.012, kRamp = 0.010;
        const size_t idx = firstEndingAfter(t - kPost);
        bool any = false;
        for (size_t e = idx; e < events.size() && events[e].t0 - kPre <= t; ++e) {
            const FixEvent& ev = events[e];
            const double amt = std::min({ 1.0, (t - (ev.t0 - kPre)) / kRamp, ((ev.t1 + kPost) - t) / kRamp });
            if (amt <= 0.0) continue;
            for (size_t j = 0; j < kCutBands; ++j) {
                const float c = static_cast<float>(ev.beatCutDb[j] * amt);
                cutsDb[j] = any ? std::max(cutsDb[j], c) : c;
            }
            any = true;
        }
        return any;
    }

    // Vocal boost from word fixes at t, active only on the consonants of the word.
    bool vocalBoostAt(double t, float* boostDb) const noexcept {
        if (events.empty()) return false;
        static constexpr double kEdge = 0.004;
        const size_t idx = firstEndingAfter(t - kEdge);
        bool any = false;
        for (size_t e = idx; e < events.size() && events[e].t0 - kEdge <= t; ++e) {
            const FixEvent& ev = events[e];
            double amt = 0.0;
            if (ev.consonants.empty()) {
                amt = std::min({ 1.0, (t - (ev.t0 - kEdge)) / kEdge, ((ev.t1 + kEdge) - t) / kEdge });
            } else {
                for (const auto& s : ev.consonants) {
                    const double v = std::min({ 1.0, (t - (s.first - kEdge)) / kEdge, ((s.second + kEdge) - t) / kEdge });
                    amt = std::max(amt, v);
                }
            }
            if (amt <= 0.0) continue;
            for (size_t j = 0; j < kBoostBands; ++j) {
                const float c = static_cast<float>(ev.vocalBoostDb[j] * amt);
                boostDb[j] = any ? std::max(boostDb[j], c) : c;
            }
            any = true;
        }
        return any;
    }
private:
    size_t firstEndingAfter(double t) const noexcept {
        size_t lo = 0, hi = events.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (events[mid].t1 < t) lo = mid + 1; else hi = mid;
        }
        return lo;
    }
};

} // namespace wers
