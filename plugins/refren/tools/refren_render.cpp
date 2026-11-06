// Writes listening samples of the sources of Refren from the user's own vocals, on the user's computer.
//   refren_render --list <text file with paths> --out <folder>
#include "RenderProbes.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv) {
    const char* list = nullptr;
    const char* out = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0 && i + 1 < argc) list = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
    }
    if (list == nullptr || out == nullptr) {
        std::printf("usage: refren_render --list <file> --out <folder>\n");
        return 2;
    }
    // Program arguments come in the code page of the system, which this turns into UTF-8.
    const int rendered = refren::renderProbes(std::filesystem::path(list).u8string(), std::filesystem::path(out).u8string());
    std::printf("rendered %d files\n", rendered);
    return rendered > 0 ? 0 : 1;
}
