#pragma once
// Measurements on the user's own songs and of the processor. Nothing here writes sound or text,
// so the results can leave the computer without the songs.
#include <string>
#include <vector>

namespace barwa {

struct SongResult {
    double seconds = 0.0;
    int sibFrames = 0, vowelFrames = 0;
    double sibDropDb = 0.0, vowelDropDb = 0.0; // how much less is added with the mask than without it
    double sibLevelDb = 0.0, vowelLevelDb = 0.0; // what is heard in those frames, against the input
    double levelDiffDb = 0.0, peakDiffDb = 0.0; // with the mask and Auto level, against the input
};

// Fixed settings: Lamp, Drive 12 dB. Sibilants and vowels are found from the frequency balance of
// each frame, without the classifier the mask uses, so the classifier cannot grade itself.
SongResult measureSong(const std::vector<float>& mono, double fs);

// Takes a text file with one WAV path a line, in UTF-8, and writes one row a line.
// A file that cannot be read gets a row with zero seconds. Returns how many were measured.
int measureList(const std::u8string& listPath, const std::u8string& outPath);

// Share of one core in percent for each character, at the heaviest settings of the specification:
// stereo noise, 48 kHz, blocks of 256, three bands, 4x. The best of three runs of the given length.
std::vector<double> measureCpu(double seconds);

} // namespace barwa
