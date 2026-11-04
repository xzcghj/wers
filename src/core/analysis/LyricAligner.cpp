#include "Recognizer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace wers {
namespace {

// Decodes UTF-8 to code points; invalid bytes are skipped.
std::u32string decode(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0; int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (i + static_cast<size_t>(extra) >= s.size() + (extra == 0 ? 1 : 0)) break;
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const auto b = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((b >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (b & 0x3F);
        }
        i += static_cast<size_t>(extra) + 1;
        if (ok) out.push_back(cp);
    }
    return out;
}

char32_t lower(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    switch (c) {
        case U'Ą': return U'ą'; case U'Ć': return U'ć'; case U'Ę': return U'ę'; case U'Ł': return U'ł';
        case U'Ń': return U'ń'; case U'Ó': return U'ó'; case U'Ś': return U'ś'; case U'Ź': return U'ź'; case U'Ż': return U'ż';
        default: return c;
    }
}

bool isWordChar(char32_t c) {
    if ((c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9')) return true;
    return c >= 0xC0 && c != 0xD7 && c != 0xF7 && !(c >= 0x2000 && c <= 0x2BFF);
}

void encode(char32_t c, std::string& o) {
    if (c < 0x80) o.push_back(static_cast<char>(c));
    else if (c < 0x800) { o.push_back(static_cast<char>(0xC0 | (c >> 6))); o.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) { o.push_back(static_cast<char>(0xE0 | (c >> 12))); o.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    else { o.push_back(static_cast<char>(0xF0 | (c >> 18))); o.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F))); o.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
}

int editDistance(const std::u32string& a, const std::u32string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

} // namespace

std::string normaliseWord(const std::string& word) {
    std::string out;
    for (char32_t c : decode(word)) {
        const char32_t l = lower(c);
        if (isWordChar(l)) encode(l, out);
    }
    return out;
}

std::vector<int8_t> markHeardWords(const std::vector<RecognizedWord>& dry, const std::vector<RecognizedWord>& mix) {
    const size_t n = dry.size(), m = mix.size();
    std::vector<int8_t> heard(n, 0);
    if (n == 0 || m == 0) return heard;
    std::vector<std::u32string> a, b;
    a.reserve(n); b.reserve(m);
    for (const auto& w : dry) a.push_back(decode(normaliseWord(w.text)));
    for (const auto& w : mix) b.push_back(decode(normaliseWord(w.text)));
    // Words further apart in time than this cannot be the same word.
    constexpr double kWindow = 1.5;
    constexpr float kGap = 0.6f, kFar = 10.0f, kSameBelow = 0.4f;
    const auto spelling = [&](size_t i, size_t j) {
        const auto len = static_cast<float>(std::max<size_t>({ a[i].size(), b[j].size(), 1u }));
        return static_cast<float>(editDistance(a[i], b[j])) / len;
    };
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    std::vector<float> dp((n + 1) * (m + 1), 0.0f);
    std::vector<uint8_t> tb((n + 1) * (m + 1), 0);
    for (size_t i = 1; i <= n; ++i) { dp[at(i, 0)] = dp[at(i - 1, 0)] + kGap; tb[at(i, 0)] = 1; }
    for (size_t j = 1; j <= m; ++j) { dp[at(0, j)] = dp[at(0, j - 1)] + kGap; tb[at(0, j)] = 2; }
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            const bool close = std::abs(dry[i - 1].t0 - mix[j - 1].t0) < kWindow;
            const float diag = dp[at(i - 1, j - 1)] + (close ? spelling(i - 1, j - 1) : kFar);
            const float up = dp[at(i - 1, j)] + kGap, left = dp[at(i, j - 1)] + kGap;
            float best = diag; uint8_t dir = 0;
            if (up < best) { best = up; dir = 1; }
            if (left < best) { best = left; dir = 2; }
            dp[at(i, j)] = best; tb[at(i, j)] = dir;
        }
    }
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        const uint8_t d = tb[at(i, j)];
        if (i > 0 && j > 0 && d == 0) {
            if (std::abs(dry[i - 1].t0 - mix[j - 1].t0) < kWindow && spelling(i - 1, j - 1) <= kSameBelow) heard[i - 1] = 1;
            --i; --j;
        } else if (i > 0 && (d == 1 || j == 0)) {
            --i;
        } else {
            --j;
        }
    }
    return heard;
}

std::vector<RecognizedWord> alignToLyrics(const std::vector<RecognizedWord>& rec, const std::string& lyrics) {
    // Lyric tokens keep their original spelling for display.
    std::vector<std::string> tokens;
    {
        std::istringstream in(lyrics);
        std::string t;
        while (in >> t) if (!normaliseWord(t).empty()) tokens.push_back(t);
    }
    if (tokens.empty() || rec.empty()) return rec;
    std::vector<std::u32string> a, b;
    for (const auto& w : rec) a.push_back(decode(normaliseWord(w.text)));
    for (const auto& t : tokens) b.push_back(decode(normaliseWord(t)));

    // Needleman-Wunsch: matching words are cheap, similar words cost by their spelling distance.
    const size_t n = a.size(), m = b.size();
    if (n * m > 4000000u) return rec; // an absurd paste: keep what was recognised
    std::vector<float> dp((n + 1) * (m + 1), 0.0f);
    std::vector<uint8_t> tb((n + 1) * (m + 1), 0);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    constexpr float kGap = 0.7f;
    for (size_t i = 1; i <= n; ++i) { dp[at(i, 0)] = dp[at(i - 1, 0)] + kGap; tb[at(i, 0)] = 1; }
    for (size_t j = 1; j <= m; ++j) { dp[at(0, j)] = dp[at(0, j - 1)] + kGap; tb[at(0, j)] = 2; }
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            const auto len = static_cast<float>(std::max<size_t>({ a[i - 1].size(), b[j - 1].size(), 1u }));
            const float sub = static_cast<float>(editDistance(a[i - 1], b[j - 1])) / len;
            const float diag = dp[at(i - 1, j - 1)] + sub, up = dp[at(i - 1, j)] + kGap, left = dp[at(i, j - 1)] + kGap;
            float best = diag; uint8_t dir = 0;
            if (up < best) { best = up; dir = 1; }
            if (left < best) { best = left; dir = 2; }
            dp[at(i, j)] = best; tb[at(i, j)] = dir;
        }
    }
    std::vector<RecognizedWord> out = rec;
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        const uint8_t d = tb[at(i, j)];
        if (i > 0 && j > 0 && d == 0) {
            const auto len = static_cast<float>(std::max<size_t>({ a[i - 1].size(), b[j - 1].size(), 1u }));
            // Only accept a replacement that is reasonably similar: otherwise timing would lie.
            if (static_cast<float>(editDistance(a[i - 1], b[j - 1])) / len <= 0.6f) {
                out[i - 1].text = tokens[j - 1];
                out[i - 1].confidence = std::max(out[i - 1].confidence, 0.9f);
            }
            --i; --j;
        } else if (i > 0 && (d == 1 || j == 0)) {
            --i;
        } else {
            --j;
        }
    }
    return out;
}

} // namespace wers
