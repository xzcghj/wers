#pragma once
// Goes through the WAV files named in a text file, one path a line in UTF-8. Used by the tools that work on
// the user's songs on the user's computer, so their results never need to name a file.
#include "WavIO.h"
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace wers::test {

// Calls callback(index from 1, mono samples or nullptr if the file cannot be read, sample rate) once for every
// line that is not empty. Returns how many files could be read.
template <typename Callback>
int forEachListedFile(const std::u8string& listPath, Callback&& callback) {
    namespace fs = std::filesystem;
    std::ifstream list(fs::path(listPath), std::ios::binary);
    if (!list) return 0;
    int index = 0, measured = 0;
    std::string line;
    while (std::getline(list, line)) {
        if (index == 0 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        ++index;
        bool done = false;
        try {
            // On Windows a line that is not valid UTF-8 throws here, and it still gets its call.
            std::ifstream file(fs::path(std::u8string(reinterpret_cast<const char8_t*>(line.data()), line.size())), std::ios::binary);
            if (file) {
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                wav::Audio audio;
                if (wav::readBytes(bytes, audio) && !audio.ch.empty() && !audio.ch[0].empty()) {
                    bytes.clear();
                    bytes.shrink_to_fit();
                    std::vector<float> mono(audio.ch[0].size(), 0.0f);
                    const float share = 1.0f / static_cast<float>(audio.ch.size());
                    for (const auto& channel : audio.ch)
                        for (size_t i = 0; i < mono.size(); ++i) mono[i] += channel[i] * share;
                    callback(index, &mono, audio.rate);
                    done = true;
                    ++measured;
                }
            }
        } catch (const std::exception&) {
        }
        if (!done) callback(index, static_cast<const std::vector<float>*>(nullptr), 0);
    }
    return measured;
}

} // namespace wers::test
