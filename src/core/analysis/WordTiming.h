#pragma once
// Places word boundaries on the syllables heard in the voice. A speech model knows the words well
// and their timing roughly. The spelling tells how many syllables a word has, and every syllable
// has a loudness peak in the vowel band, so matching the two gives boundaries in the right valleys.
#include "Frames.h"
#include <string>
#include <vector>

namespace wers {

// Syllables of a word from its spelling. A vowel letter is one syllable, except an i that only
// softens the next vowel, as in "nie" or "się". Consonant-only words like "w" have none.
int syllableCount(const std::string& word);

struct SyllablePeak { int frame; float db; };
// Loudness peaks of the vowel band between two frames, at least 70 ms apart.
std::vector<SyllablePeak> findSyllablePeaks(const BandFrames& f, int firstFrame, int lastFrame);

// spans holds the rough times of the words and is updated in place. Words stay in order and never
// overlap. A word whose syllables cannot be found keeps its rough place between its neighbours.
void snapWordsToSyllables(const BandFrames& f, const std::vector<std::string>& words, std::vector<TimeSpan>& spans);

} // namespace wers
