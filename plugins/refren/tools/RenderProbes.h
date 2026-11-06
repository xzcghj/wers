#pragma once
// Listening samples of the sources of Refren on the user's own vocals. They stay on the user's computer, and
// their names say nothing about the song.
#include <string>

namespace refren {

// Takes a text file with one WAV path a line, in UTF-8. For every file writes 8 samples of up to 20 seconds into
// the folder, each on its own and together with the voice. Returns how many files could be read.
int renderProbes(const std::u8string& listPath, const std::u8string& outFolder);

} // namespace refren
