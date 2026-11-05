// Measures Barwa on the user's computer. The results are numbers only and never name a song.
//   barwa_measure --list <text file with paths> --out <results.tsv>
//   barwa_measure --cpu
#include "MeasureSong.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {
constexpr double kCpuSeconds = 30.0, kCpuLimitPercent = 5.0;
// Program arguments come in the code page of the system, which this turns into UTF-8.
std::u8string asPath(const char* text) { return std::filesystem::path(text).u8string(); }
}

int main(int argc, char** argv) {
    const char* list = nullptr;
    const char* out = nullptr;
    bool cpu = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0 && i + 1 < argc) list = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else if (std::strcmp(argv[i], "--cpu") == 0) cpu = true;
    }
    if (cpu) {
        const auto results = barwa::measureCpu(kCpuSeconds);
        for (size_t i = 0; i < results.size(); ++i) std::printf("character %d: %.2f %% of one core\n", static_cast<int>(i), results[i]);
        const double worst = *std::max_element(results.begin(), results.end());
        std::printf("worst %.2f %%, limit %.2f %%\n", worst, kCpuLimitPercent);
        return worst <= kCpuLimitPercent ? 0 : 1;
    }
    if (list != nullptr && out != nullptr) {
        const int measured = barwa::measureList(asPath(list), asPath(out));
        std::printf("measured %d files\n", measured);
        return measured > 0 ? 0 : 1;
    }
    std::printf("usage: barwa_measure --list <file> --out <file> | --cpu\n");
    return 2;
}
