#pragma once
// Word intelligibility model: ANSI S3.5-1997 Speech Intelligibility Index, one-third-octave procedure,
// evaluated for each short-time frame of the masker (the idea of the Extended SII).
#include "Types.h"
#include <cmath>

namespace wers {

// Standard procedure. E and N are spectrum levels in dB SPL/Hz. Returns SII in 0..1.
// audibility, when given, receives the band audibility A_i.
double siiFromLevels(const float* E, const float* N, float* audibility = nullptr) noexcept;

// Transfer function from SII to expected word recognition, 0..100.
float scoreFromSii(double sii) noexcept;

// How one dB of cut or boost in a bell band spreads over the SII bands (linearised at 6 dB).
const std::array<std::array<float, kSiiBands>, kCutBands>& cutSpread();
const std::array<std::array<float, kSiiBands>, kBoostBands>& boostSpread();

// What the mix around the word does at the moment.
struct MixState {
    float placeDepth = 0.0f;   // live Miejsce running on the beat
    // Faders come after the plugin, so the captured signals do not contain them.
    float vocalFaderDb = 0.0f;
    float beatFaderDb = 0.0f;
    bool sameAs(const MixState& o) const noexcept {
        return std::abs(placeDepth - o.placeDepth) < 1e-4f && std::abs(vocalFaderDb - o.vocalFaderDb) < 0.05f && std::abs(beatFaderDb - o.beatFaderDb) < 0.05f;
    }
};

struct ScoreMods {
    MixState mix;
    const float* vocalBoostDb = nullptr;      // kBoostBands, applied on consonant frames
    const float* beatCutDb = nullptr;         // kCutBands, applied for the whole word
};

struct WordScore {
    float score = 100.0f;
    double sii = 1.0;
    int worstBand = -1;
    Cause cause = Cause::None;
};

// Scores frames [first, last). stride > 1 skips frames for quick trial evaluations.
WordScore scoreWord(const BandFrames& f, int first, int last, Device d, const ScoreMods& mods, int stride = 1);

} // namespace wers
