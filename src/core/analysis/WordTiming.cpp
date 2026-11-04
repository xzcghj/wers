#include "WordTiming.h"
#include "Recognizer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wers {

namespace {

// Vowel band of a frame: where the energy of a syllable nucleus sits.
float vowelBandDb(const BandFrames& f, int frame) noexcept {
    const float* v = f.v10(frame);
    double s = 0.0;
    for (int j = 0; j < 5; ++j) s += std::pow(10.0, static_cast<double>(v[j]) * 0.1); // 250 Hz to 1.5 kHz
    return powerToDb(s);
}

// How the first bytes of a UTF-8 Polish vowel look.
bool isVowelAt(const std::string& w, size_t i, size_t& length) noexcept {
    const auto c = static_cast<unsigned char>(w[i]);
    if (c < 0x80) {
        length = 1;
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
    }
    if (i + 1 < w.size()) {
        const auto d = static_cast<unsigned char>(w[i + 1]);
        length = 2;
        if (c == 0xC4 && (d == 0x85 || d == 0x99)) return true; // ą ę
        if (c == 0xC3 && d == 0xB3) return true;                // ó
    }
    length = c >= 0xE0 ? 3 : 2;
    return false;
}

} // namespace

int syllableCount(const std::string& raw) {
    const std::string w = normaliseWord(raw);
    int count = 0;
    for (size_t i = 0; i < w.size();) {
        size_t length = 1;
        const bool vowel = isVowelAt(w, i, length);
        if (vowel) {
            bool softens = false;
            if (w[i] == 'i' && i + length < w.size()) {
                size_t nextLength = 1;
                softens = isVowelAt(w, i + length, nextLength);
            }
            if (!softens) ++count;
        }
        i += length;
    }
    return count;
}

std::vector<SyllablePeak> findSyllablePeaks(const BandFrames& f, int firstFrame, int lastFrame) {
    std::vector<SyllablePeak> out;
    const int a = std::clamp(firstFrame, 0, f.count), b = std::clamp(lastFrame, 0, f.count);
    if (b - a < 5) return out;
    const auto n = static_cast<size_t>(b - a);
    std::vector<float> level(n);
    for (size_t i = 0; i < n; ++i) level[i] = vowelBandDb(f, a + static_cast<int>(i));
    // Short smoothing removes the ripple of a low voice without melting syllables together.
    std::vector<float> s(n);
    for (size_t i = 0; i < n; ++i) {
        const float before = level[i > 0 ? i - 1 : i], after = level[i + 1 < n ? i + 1 : i];
        s[i] = 0.25f * before + 0.5f * level[i] + 0.25f * after;
    }
    auto sorted = s;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n * 95 / 100), sorted.end());
    const float floorDb = sorted[n * 95 / 100] - 38.0f;

    constexpr int kMinGap = 7;           // frames: 70 ms between syllables is faster than any rap
    constexpr float kProminence = 3.0f;  // dB the peak must stand above the valleys beside it
    for (size_t i = 1; i + 1 < n; ++i) {
        if (s[i] < floorDb || s[i] < s[i - 1] || s[i] <= s[i + 1]) continue;
        // The lowest point on each side, up to the next taller sound or 250 ms away.
        float leftLow = s[i], rightLow = s[i];
        for (size_t k = i; k-- > 0 && i - k <= 25 && s[k] <= s[i];) leftLow = std::min(leftLow, s[k]);
        for (size_t k = i + 1; k < n && k - i <= 25 && s[k] <= s[i]; ++k) rightLow = std::min(rightLow, s[k]);
        const float leftDepth = s[i] - leftLow, rightDepth = s[i] - rightLow;
        if (std::min(leftDepth, rightDepth) < kProminence) continue;
        const int frame = a + static_cast<int>(i);
        if (!out.empty() && frame - out.back().frame < kMinGap) {
            if (s[i] > out.back().db) out.back() = { frame, s[i] };
            continue;
        }
        out.push_back({ frame, s[i] });
    }
    return out;
}

void snapWordsToSyllables(const BandFrames& f, const std::vector<std::string>& words, std::vector<TimeSpan>& spans) {
    const size_t w = spans.size();
    if (w == 0 || f.count == 0 || words.size() != w) return;
    const auto margin = static_cast<int>(1.0 / kFrameSeconds);
    const auto peaks = findSyllablePeaks(f, frameAtTime(spans.front().t0) - margin, frameAtTime(spans.back().t1) + margin);
    const size_t m = peaks.size();
    std::vector<int> expected(w);
    for (size_t i = 0; i < w; ++i) expected[i] = syllableCount(words[i]);

    // Dynamic programming over words and peaks. cost[i][j]: first i words took first j peaks.
    constexpr float kInfinite = std::numeric_limits<float>::max() / 4.0f;
    constexpr float kSyllableMiss = 1.0f;     // one syllable too many or too few
    constexpr float kNoPeakAtAll = 0.8f;      // a word with syllables and no peak is rarely right
    constexpr float kPerSecondOff = 3.0f;     // being a third of a second off counts as one syllable
    constexpr float kFreeMargin = 0.08f;      // the model's timing is trusted this far
    constexpr float kSkipPeak = 0.45f;        // a peak that belongs to no word, like an ad-lib
    constexpr int kMaxExtra = 2;
    const size_t stride = m + 1;
    std::vector<float> cost((w + 1) * stride, kInfinite);
    std::vector<uint8_t> took((w + 1) * stride, 255); // peaks taken by word i ending at peak j, 254 means peak j skipped
    const auto at = [stride](size_t i, size_t j) { return i * stride + j; };
    cost[at(0, 0)] = 0.0f;
    for (size_t j = 1; j <= m; ++j) { cost[at(0, j)] = cost[at(0, j - 1)] + kSkipPeak; took[at(0, j)] = 254; }
    for (size_t i = 1; i <= w; ++i) {
        const int want = expected[i - 1];
        const auto r0 = static_cast<float>(spans[i - 1].t0), r1 = static_cast<float>(spans[i - 1].t1);
        for (size_t j = 0; j <= m; ++j) {
            float best = kInfinite;
            uint8_t how = 255;
            if (j > 0 && cost[at(i, j - 1)] + kSkipPeak < best) { best = cost[at(i, j - 1)] + kSkipPeak; how = 254; }
            const int most = want + kMaxExtra;
            for (int k = 0; k <= most && static_cast<size_t>(k) <= j; ++k) {
                const float before = cost[at(i - 1, j - static_cast<size_t>(k))];
                if (before >= kInfinite) continue;
                float c = before + kSyllableMiss * static_cast<float>(std::abs(k - want));
                if (k == 0 && want > 0) c += kNoPeakAtAll;
                if (k > 0) {
                    const auto first = static_cast<float>(frameCentre(peaks[j - static_cast<size_t>(k)].frame));
                    const auto last = static_cast<float>(frameCentre(peaks[j - 1].frame));
                    c += kPerSecondOff * (std::max(0.0f, r0 - kFreeMargin - first) + std::max(0.0f, last - r1 - kFreeMargin));
                }
                if (c < best) { best = c; how = static_cast<uint8_t>(k); }
            }
            cost[at(i, j)] = best;
            took[at(i, j)] = how;
        }
    }

    // Walk back: which peaks each word got.
    struct Assigned { int first = -1, last = -1; };
    std::vector<Assigned> assigned(w);
    size_t i = w, j = m;
    while (i > 0) {
        const uint8_t how = took[at(i, j)];
        if (how == 254) { --j; continue; }
        if (how == 255) break;
        if (how > 0) { assigned[i - 1].first = static_cast<int>(j) - how; assigned[i - 1].last = static_cast<int>(j) - 1; }
        j -= how;
        --i;
    }

    const auto levelAt = [&f](int frame) { return frameVoiceLevelDb(f.v10(std::clamp(frame, 0, f.count - 1))); };
    // Where the sound rises out of quiet before a peak, or falls into it after.
    constexpr float kEdgeDrop = 22.0f;
    constexpr int kEdgeSearch = 30;
    const auto onsetBefore = [&](int peakFrame, int limitFrame) {
        const float edge = levelAt(peakFrame) - kEdgeDrop;
        for (int k = peakFrame; k > std::max(limitFrame, peakFrame - kEdgeSearch); --k) if (levelAt(k - 1) < edge) return k;
        return std::max(limitFrame, peakFrame - kEdgeSearch);
    };
    const auto offsetAfter = [&](int peakFrame, int limitFrame) {
        const float edge = levelAt(peakFrame) - kEdgeDrop;
        for (int k = peakFrame; k < std::min(limitFrame, peakFrame + kEdgeSearch); ++k) if (levelAt(k + 1) < edge) return k + 1;
        return std::min(limitFrame, peakFrame + kEdgeSearch);
    };

    // Boundaries between words that have peaks: the valley, or the edges of a real pause.
    std::vector<int> startFrame(w, -1), endFrame(w, -1);
    int previous = -1;
    for (size_t k = 0; k < w; ++k) {
        if (assigned[k].first < 0) continue;
        const int firstPeak = peaks[static_cast<size_t>(assigned[k].first)].frame;
        if (previous < 0) {
            startFrame[k] = onsetBefore(firstPeak, 0);
        } else {
            const int lastPeak = peaks[static_cast<size_t>(assigned[static_cast<size_t>(previous)].last)].frame;
            int valley = (lastPeak + firstPeak) / 2;
            float low = std::numeric_limits<float>::max();
            for (int q = lastPeak + 1; q < firstPeak; ++q) {
                const float v = vowelBandDb(f, q);
                if (v < low) { low = v; valley = q; }
            }
            const float deep = std::min(vowelBandDb(f, lastPeak), vowelBandDb(f, firstPeak)) - low;
            if (deep > 25.0f && firstPeak - lastPeak > 25) {
                endFrame[static_cast<size_t>(previous)] = offsetAfter(lastPeak, valley);
                startFrame[k] = onsetBefore(firstPeak, valley);
            } else {
                endFrame[static_cast<size_t>(previous)] = valley;
                startFrame[k] = valley;
            }
        }
        previous = static_cast<int>(k);
    }
    if (previous >= 0) endFrame[static_cast<size_t>(previous)] = offsetAfter(peaks[static_cast<size_t>(assigned[static_cast<size_t>(previous)].last)].frame, f.count - 1);

    // Back to seconds, then words without peaks get room between their neighbours.
    std::vector<TimeSpan> result = spans;
    for (size_t k = 0; k < w; ++k) {
        if (startFrame[k] >= 0) {
            result[k].t0 = frameCentre(startFrame[k]);
            result[k].t1 = frameCentre(endFrame[k]);
        }
    }
    const auto shortest = [&expected](size_t k) { return expected[k] > 0 ? 0.11 : 0.05; };
    for (size_t k = 0; k < w; ++k) {
        if (startFrame[k] >= 0) continue;
        size_t next = k;
        while (next < w && startFrame[next] < 0) ++next;
        double from = k > 0 ? result[k - 1].t1 : std::max(0.0, spans[k].t0 - 0.2);
        double to = next < w ? result[next].t0 : spans[w - 1].t1 + 0.2;
        double need = 0.0, rough = 0.0, weightSum = 0.0;
        for (size_t q = k; q < next; ++q) {
            need += shortest(q);
            rough += std::max(shortest(q), spans[q].t1 - spans[q].t0);
            weightSum += std::max(0.5, static_cast<double>(expected[q]));
        }
        // Too little room: take some from the neighbours, but never their syllable peaks.
        if (to - from < need) {
            if (k > 0 && assigned[k - 1].last >= 0) {
                from = std::min(from, frameCentre(peaks[static_cast<size_t>(assigned[k - 1].last)].frame) + 0.05);
                result[k - 1].t1 = from;
            }
            if (next < w && assigned[next].first >= 0) {
                to = std::max(to, frameCentre(peaks[static_cast<size_t>(assigned[next].first)].frame) - 0.05);
                result[next].t0 = to;
            }
        }
        if (to - from > rough * 1.3) {
            // A real pause around them: they stay near where the model heard them.
            double t = from;
            double left = rough;
            for (size_t q = k; q < next; ++q) {
                const double length = std::max(shortest(q), spans[q].t1 - spans[q].t0);
                const double start = std::clamp(spans[q].t0, t, std::max(t, to - left));
                result[q].t0 = start;
                result[q].t1 = start + length;
                t = result[q].t1;
                left -= length;
            }
        } else {
            // Shared by syllables, or side by side at the shortest length when even that does not fit.
            const double available = std::max(to - from, need);
            double t = from;
            for (size_t q = k; q < next; ++q) {
                const double length = std::max(shortest(q), available * std::max(0.5, static_cast<double>(expected[q])) / weightSum);
                result[q].t0 = t;
                result[q].t1 = t + length;
                t = result[q].t1;
            }
        }
        k = next - 1;
        if (next >= w) break;
    }
    // Nothing may overlap or collapse, whatever the steps above met.
    for (size_t k = 0; k < w; ++k) {
        if (k > 0) result[k].t0 = std::max(result[k].t0, result[k - 1].t1);
        result[k].t1 = std::max(result[k].t1, result[k].t0 + 0.03);
    }
    spans = result;
}

} // namespace wers
