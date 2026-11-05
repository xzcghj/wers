#pragma once
// Minimal WAV reader and writer for the offline tools (PCM 16/24/32 and float 32).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wers::wav {

struct Audio { int rate = 0; int channels = 0; std::vector<std::vector<float>> ch; };

inline bool readBytes(const std::vector<uint8_t>& d, Audio& a) {
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
    int fmt = 0, bits = 0; size_t p = 12;
    while (p + 8 <= d.size()) {
        const uint32_t len = d[p + 4] | (d[p + 5] << 8) | (d[p + 6] << 16) | (static_cast<uint32_t>(d[p + 7]) << 24);
        if (!std::memcmp(d.data() + p, "fmt ", 4)) {
            fmt = d[p + 8] | (d[p + 9] << 8);
            a.channels = d[p + 10];
            a.rate = static_cast<int>(d[p + 12] | (d[p + 13] << 8) | (d[p + 14] << 16));
            bits = d[p + 22];
            if (fmt == 0xFFFE) fmt = d[p + 32];
        } else if (!std::memcmp(d.data() + p, "data", 4)) {
            const size_t bytes = bits / 8, n = (std::min<size_t>(len, d.size() - p - 8)) / (bytes * static_cast<size_t>(a.channels));
            a.ch.assign(static_cast<size_t>(a.channels), std::vector<float>(n));
            const uint8_t* s = d.data() + p + 8;
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < a.channels; ++c, s += bytes) {
                    float v = 0;
                    if (fmt == 3 && bits == 32) std::memcpy(&v, s, 4);
                    else if (bits == 16) v = static_cast<float>(static_cast<int16_t>(s[0] | (s[1] << 8))) / 32768.0f;
                    else if (bits == 24) v = static_cast<float>(static_cast<int32_t>((s[0] << 8) | (s[1] << 16) | (static_cast<uint32_t>(s[2]) << 24))) / 2147483648.0f;
                    else if (bits == 32) v = static_cast<float>(static_cast<int32_t>(s[0] | (s[1] << 8) | (s[2] << 16) | (static_cast<uint32_t>(s[3]) << 24))) / 2147483648.0f;
                    a.ch[static_cast<size_t>(c)][i] = v;
                }
            return true;
        }
        p += 8 + len + (len & 1u);
    }
    return false;
}

inline bool read(const std::string& path, Audio& a) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); const long size = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(static_cast<size_t>(size));
    const size_t got = std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    if (got != d.size()) return false;
    return readBytes(d, a);
}

inline bool write(const std::string& path, const Audio& a) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t frames = a.ch.empty() ? 0 : static_cast<uint32_t>(a.ch[0].size());
    const uint32_t bytes = frames * static_cast<uint32_t>(a.channels) * 4u;
    const auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(3); w16(static_cast<uint16_t>(a.channels));
    w32(static_cast<uint32_t>(a.rate)); w32(static_cast<uint32_t>(a.rate * a.channels * 4)); w16(static_cast<uint16_t>(a.channels * 4)); w16(32);
    std::fwrite("data", 1, 4, f); w32(bytes);
    for (uint32_t i = 0; i < frames; ++i) for (int c = 0; c < a.channels; ++c) std::fwrite(&a.ch[static_cast<size_t>(c)][i], 4, 1, f);
    std::fclose(f);
    return true;
}

} // namespace wers::wav
