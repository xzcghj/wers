#include "FixPlanner.h"

namespace wers {

void scoreAllDevices(const BandFrames& f, Word& w, const MixState& mix) {
    const int a = frameAtTime(w.t0), b = frameAtTime(w.t1);
    for (int d = 0; d < kNumDevices; ++d) {
        auto& v = w.verdict[static_cast<size_t>(d)];
        ScoreMods base; base.mix = mix;
        const WordScore s0 = scoreWord(f, a, b, static_cast<Device>(d), base);
        v.baseScore = s0.score;
        v.score = s0.score; v.worstBand = static_cast<int8_t>(s0.worstBand); v.cause = s0.cause;
        if (w.fix.active) {
            ScoreMods m = base;
            m.vocalBoostDb = w.fix.vocalBoostDb.data();
            m.beatCutDb = w.fix.beatCutDb.data();
            const WordScore s1 = scoreWord(f, a, b, static_cast<Device>(d), m);
            v.score = s1.score; v.worstBand = static_cast<int8_t>(s1.worstBand); v.cause = s1.cause;
        }
    }
}

WordFix planFix(const BandFrames& f, const Word& word, Device target, const MixState& mix, const PlannerLimits& limits) {
    WordFix fix;
    fix.target = target;
    const int a = frameAtTime(word.t0), b = frameAtTime(word.t1);
    const int stride = b - a > 40 ? 2 : 1;
    ScoreMods mods;
    mods.mix = mix;
    mods.vocalBoostDb = fix.vocalBoostDb.data();
    mods.beatCutDb = fix.beatCutDb.data();
    float current = scoreWord(f, a, b, target, mods, stride).score;
    if (current >= kScoreTarget) return fix;

    constexpr float kStep = 0.5f;
    // Cuts below 630 Hz barely help words and take weight out of the beat, so they are left alone.
    constexpr size_t kFirstCut = 2, kLastCut = kCutBands - 1;
    for (int iter = 0; iter < 48; ++iter) {
        float bestGain = 0.0f;
        int bestBoost = -1, bestCut = -1;
        for (size_t j = 0; j < kBoostBands; ++j) {
            if (fix.vocalBoostDb[j] + kStep > limits.maxBoostDb) continue;
            fix.vocalBoostDb[j] += kStep;
            const float gain = scoreWord(f, a, b, target, mods, stride).score - current;
            fix.vocalBoostDb[j] -= kStep;
            if (gain > bestGain) { bestGain = gain; bestBoost = static_cast<int>(j); bestCut = -1; }
        }
        for (size_t j = kFirstCut; j <= kLastCut; ++j) {
            if (fix.beatCutDb[j] + kStep > limits.maxCutDb) continue;
            fix.beatCutDb[j] += kStep;
            const float gain = scoreWord(f, a, b, target, mods, stride).score - current;
            fix.beatCutDb[j] -= kStep;
            if (gain > bestGain) { bestGain = gain; bestCut = static_cast<int>(j); bestBoost = -1; }
        }
        if (bestGain < 0.02f) break;
        if (bestBoost >= 0) fix.vocalBoostDb[static_cast<size_t>(bestBoost)] += kStep;
        else fix.beatCutDb[static_cast<size_t>(bestCut)] += kStep;
        current += bestGain;
        if (current >= kScoreTarget) break;
    }
    for (float v : fix.vocalBoostDb) fix.active = fix.active || v > 0.0f;
    for (float v : fix.beatCutDb) fix.active = fix.active || v > 0.0f;
    return fix;
}

} // namespace wers
