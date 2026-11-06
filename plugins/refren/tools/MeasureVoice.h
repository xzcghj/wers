#pragma once
// Measurements on the user's own vocals and of the processor. Nothing here writes sound or text, so the results can
// leave the computer without the songs.
#include <string>
#include <vector>

namespace refren {

struct SongNumbers {
    double seconds = 0.0;
    double voicedShare = 0.0;      // of the loud moments, 0 to 1
    double pitchSpreadCents = 0.0; // the middle step between neighbouring readings
    double sibSideDropDb = 0.0;    // how much less the sides hold in sibilants with the mask than without it
    double levelDiffDb = 0.0;      // one layer at its defaults against the input, with weighting K
};

SongNumbers measureVoice(const std::vector<float>& mono, double fs);

// Takes a text file with one WAV path a line, in UTF-8, and writes one row a line.
// A file that cannot be read gets a row with zero seconds. Returns how many were measured.
int measureVoiceList(const std::u8string& listPath, const std::u8string& outPath);

// Share of one core in percent for the heaviest settings of the specification: four layers, 32 voices, grain, echo and
// reverb with shimmer, stereo, 48 kHz, blocks of 256. The best of three runs of the given length.
std::vector<double> measureCpu(double seconds);

} // namespace refren
