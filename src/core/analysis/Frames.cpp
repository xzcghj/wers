#include "Frames.h"
#include "Fft.h"
#include "dsp/Loudness.h"
#include "dsp/PhonemeClassifier.h"

namespace wers {
namespace {

constexpr int kWindow = 512, kFft = 1024;
constexpr double kTopHz = 7600.0;

struct BandMap {
    std::array<std::pair<int, int>, kSiiBands> sii{};
    std::array<std::pair<int, int>, kCutBands> cut{};
    double binHz = 0.0;
    BandMap() {
        binHz = static_cast<double>(kAnalysisRate) / kFft;
        const auto bin = [this](double f) { return std::clamp(static_cast<int>(std::lround(f / binHz)), 1, kFft / 2); };
        const int top = bin(kTopHz);
        for (size_t i = 0; i < kSiiBands; ++i) {
            int lo = bin(kSiiFreq[i] / std::pow(2.0, 1.0 / 6.0));
            int hi = bin(kSiiFreq[i] * std::pow(2.0, 1.0 / 6.0));
            hi = std::min(hi, top);
            if (lo >= hi) lo = hi - 1;
            sii[i] = { lo, hi };
        }
        // Edges of the -3 dB points of the realtime bell, so both measure the same band.
        const double half = 1.0 / (2.0 * kCutQ), root = std::sqrt(1.0 + half * half);
        for (size_t j = 0; j < kCutBands; ++j) {
            const int lo = bin(kCutCentres[j] * (root - half));
            const int hi = std::min(bin(kCutCentres[j] * (root + half)), top);
            cut[j] = { lo, hi };
        }
    }
};

// One frame of the two PSD-derived band sets.
void bandsFromSpectrum(const std::vector<double>& psd, const BandMap& map, float* out18, float* out10) {
    for (size_t i = 0; i < kSiiBands; ++i) {
        double s = 0.0;
        const auto [lo, hi] = map.sii[i];
        for (int b = lo; b < hi; ++b) s += psd[static_cast<size_t>(b)];
        out18[i] = powerToDb(s / std::max(1, hi - lo), -150.0f);
    }
    for (size_t j = 0; j < kCutBands; ++j) {
        const auto [lo, hi] = map.cut[j];
        if (hi <= lo) { out10[j] = -140.0f; continue; } // above the analysis bandwidth
        double s = 0.0;
        for (int b = lo; b < hi; ++b) s += psd[static_cast<size_t>(b)];
        // pi/2 converts a flat band sum to the noise bandwidth of the realtime bell output.
        out10[j] = powerToDb(s * map.binHz * 1.5708, -140.0f);
    }
}

} // namespace

BandFrames computeFrames(const std::vector<float>& vocal, const std::vector<float>& beat, const std::function<bool(float)>& progress) {
    BandFrames f;
    const size_t n = vocal.size();
    if (n < static_cast<size_t>(kWindow)) return f;
    f.count = static_cast<int>((n - kWindow) / kFrameHop) + 1;
    const auto count = static_cast<size_t>(f.count);
    f.vocal18.assign(count * kSiiBands, -150.0f);
    f.beat18.assign(count * kSiiBands, -150.0f);
    f.vocal10.assign(count * kCutBands, -140.0f);
    f.beat10.assign(count * kCutBands, -140.0f);
    f.cls.assign(count, SoundClass::Silence);

    static const BandMap map;
    const Fft fft(kFft);
    std::array<double, kWindow> win{};
    double winPower = 0.0;
    for (int i = 0; i < kWindow; ++i) {
        win[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (i + 0.5) / kWindow);
        winPower += win[static_cast<size_t>(i)] * win[static_cast<size_t>(i)];
    }
    const double scale = 2.0 / (kAnalysisRate * winPower);
    std::vector<std::complex<double>> buf(kFft);
    std::vector<double> psd(kFft / 2 + 1);
    const auto analyse = [&](const std::vector<float>& x, size_t start, float* o18, float* o10) {
        for (int i = 0; i < kFft; ++i) {
            const size_t p = start + static_cast<size_t>(i);
            const double v = i < kWindow && p < x.size() ? static_cast<double>(x[p]) * win[static_cast<size_t>(i)] : 0.0;
            buf[static_cast<size_t>(i)] = { v, 0.0 };
        }
        fft.transform(buf);
        for (int b = 0; b <= kFft / 2; ++b) psd[static_cast<size_t>(b)] = std::norm(buf[static_cast<size_t>(b)]) * scale;
        bandsFromSpectrum(psd, map, o18, o10);
    };
    for (int fr = 0; fr < f.count; ++fr) {
        const size_t start = static_cast<size_t>(fr) * kFrameHop;
        analyse(vocal, start, f.vocal18.data() + static_cast<size_t>(fr) * kSiiBands, f.vocal10.data() + static_cast<size_t>(fr) * kCutBands);
        if (!beat.empty()) analyse(beat, start, f.beat18.data() + static_cast<size_t>(fr) * kSiiBands, f.beat10.data() + static_cast<size_t>(fr) * kCutBands);
        if (progress && (fr & 255) == 0 && !progress(0.8f * static_cast<float>(fr) / static_cast<float>(f.count))) { f.count = 0; return f; }
    }

    // Mix loudness, from the same signals the listener hears.
    LoudnessMeter meter, vocalMeter, beatMeter;
    meter.prepare(kAnalysisRate);
    vocalMeter.prepare(kAnalysisRate);
    beatMeter.prepare(kAnalysisRate);
    std::vector<float> mix(4096), beatPart(4096);
    for (size_t pos = 0; pos < n; pos += mix.size()) {
        const size_t len = std::min(mix.size(), n - pos);
        for (size_t i = 0; i < len; ++i) {
            beatPart[i] = pos + i < beat.size() ? beat[pos + i] : 0.0f;
            mix[i] = vocal[pos + i] + beatPart[i];
        }
        const float* ch[1] = { mix.data() };
        meter.process(ch, 1, static_cast<int>(len));
        const float* vocalCh[1] = { vocal.data() + pos };
        vocalMeter.process(vocalCh, 1, static_cast<int>(len));
        const float* beatCh[1] = { beatPart.data() };
        beatMeter.process(beatCh, 1, static_cast<int>(len));
    }
    f.lufs = meter.integratedLufs();
    f.lufsVocal = vocalMeter.integratedLufs();
    f.lufsBeat = beatMeter.integratedLufs();

    // Sound classes: the same classifier as the realtime path, averaged over each 10 ms step and
    // assigned to the frame whose window centre falls in that step.
    PhonemeClassifier c;
    c.prepare(kAnalysisRate);
    std::array<float, 5> acc{};
    int hops = 0;
    for (size_t i = 0; i < n; ++i) {
        if (c.push(vocal[i])) {
            const auto& m = c.current();
            acc[0] += m.silence; acc[1] += m.vowel; acc[2] += m.sib; acc[3] += m.stop; acc[4] += m.breath;
            ++hops;
        }
        if ((i + 1) % kFrameHop == 0) {
            const int centred = static_cast<int>((i + 1) / kFrameHop) - 2;
            if (centred >= 0 && centred < f.count && hops > 0) {
                size_t best = 0;
                for (size_t k = 1; k < acc.size(); ++k) if (acc[k] > acc[best]) best = k;
                f.cls[static_cast<size_t>(centred)] = static_cast<SoundClass>(best);
            }
            acc.fill(0.0f); hops = 0;
        }
    }
    if (progress) progress(1.0f);
    return f;
}

float frameVoiceLevelDb(const float* v10) noexcept {
    double s = 0.0;
    for (int j = 0; j < kCutBands - 1; ++j) s += std::pow(10.0, v10[j] * 0.1);
    return powerToDb(s);
}

std::vector<TimeSpan> segmentVoice(const BandFrames& f) {
    std::vector<TimeSpan> out;
    if (f.count == 0) return out;
    std::vector<float> lvl(static_cast<size_t>(f.count));
    for (int i = 0; i < f.count; ++i) lvl[static_cast<size_t>(i)] = frameVoiceLevelDb(f.v10(i));
    auto sorted = lvl;
    std::sort(sorted.begin(), sorted.end());
    const float p95 = sorted[static_cast<size_t>(sorted.size() * 95 / 100)];
    const float gate = std::max(p95 - 32.0f, -70.0f);

    // Runs above the gate, gaps below 60 ms are closed.
    std::vector<std::pair<int, int>> runs;
    int start = -1, quiet = 0;
    for (int i = 0; i < f.count; ++i) {
        if (lvl[static_cast<size_t>(i)] >= gate) {
            if (start < 0) start = i;
            quiet = 0;
        } else if (start >= 0 && ++quiet > 6) {
            runs.emplace_back(start, i - quiet + 1);
            start = -1; quiet = 0;
        }
    }
    if (start >= 0) runs.emplace_back(start, f.count);

    // Split long runs at valleys, so fragments are about word sized.
    for (auto [a, b] : runs) {
        int s = a;
        while (b - s > 70) {
            int bestAt = -1; float bestDepth = 3.0f;
            for (int i = s + 15; i < std::min(b - 10, s + 65); ++i) {
                const float here = lvl[static_cast<size_t>(i)];
                float left = here, right = here;
                for (int k = std::max(s, i - 10); k < i; ++k) left = std::max(left, lvl[static_cast<size_t>(k)]);
                for (int k = i + 1; k < std::min(b, i + 11); ++k) right = std::max(right, lvl[static_cast<size_t>(k)]);
                const float depth = std::min(left, right) - here;
                if (depth > bestDepth) { bestDepth = depth; bestAt = i; }
            }
            if (bestAt < 0) bestAt = std::min(b - 10, s + 50);
            if (bestAt - s >= 8) out.push_back({ frameCentre(s), frameCentre(bestAt) });
            s = bestAt;
        }
        if (b - s >= 8) out.push_back({ frameCentre(s), frameCentre(b) });
    }
    return out;
}

void refineWordEdges(const BandFrames& f, std::vector<TimeSpan>& spans) {
    if (f.count == 0) return;
    const auto levelAt = [&](int i) { return frameVoiceLevelDb(f.v10(std::clamp(i, 0, f.count - 1))); };
    for (size_t w = 0; w < spans.size(); ++w) {
        const double prevEnd = w > 0 ? spans[w - 1].t1 : 0.0;
        const double nextStart = w + 1 < spans.size() ? spans[w + 1].t0 : 1e9;
        int a = frameAtTime(spans[w].t0), b = frameAtTime(spans[w].t1);
        float peak = -200.0f;
        for (int i = a; i < b; ++i) peak = std::max(peak, levelAt(i));
        const float edge = peak - 20.0f;
        // Pull the start to where the sound really begins, up to 120 ms in either direction.
        int bestA = a;
        for (int i = a + 12; i >= a - 12; --i) if (levelAt(i) >= edge && levelAt(i - 1) < edge) { bestA = i; if (i <= a) break; }
        int bestB = b;
        for (int i = b - 12; i <= b + 12; ++i) if (levelAt(i) >= edge && levelAt(i + 1) < edge) { bestB = i + 1; if (i >= b) break; }
        const double t0 = frameCentre(bestA), t1 = frameCentre(bestB);
        if (t1 - t0 > 0.04) {
            spans[w].t0 = std::max(t0, prevEnd);
            spans[w].t1 = std::min(t1, nextStart);
            if (spans[w].t1 <= spans[w].t0) spans[w].t1 = spans[w].t0 + 0.04;
        }
    }
}

std::vector<std::pair<float, float>> consonantSpans(const BandFrames& f, double t0, double t1) {
    std::vector<std::pair<float, float>> out;
    const int a = std::clamp(frameAtTime(t0), 0, f.count);
    const int b = std::clamp(frameAtTime(t1), 0, f.count);
    int s = -1;
    for (int i = a; i < b; ++i) {
        const bool c = isConsonant(f.cls[static_cast<size_t>(i)]);
        if (c && s < 0) s = i;
        if (!c && s >= 0) { out.emplace_back(static_cast<float>(frameCentre(s)), static_cast<float>(frameCentre(i))); s = -1; }
    }
    if (s >= 0) out.emplace_back(static_cast<float>(frameCentre(s)), static_cast<float>(frameCentre(b)));
    return out;
}

float voiceReferenceDb(const BandFrames& f) {
    if (f.count == 0) return -60.0f;
    std::vector<float> lvl;
    lvl.reserve(static_cast<size_t>(f.count));
    for (int i = 0; i < f.count; ++i) {
        if (f.cls[static_cast<size_t>(i)] == SoundClass::Vowel) lvl.push_back(frameVoiceLevelDb(f.v10(i)));
    }
    if (lvl.empty()) return -60.0f;
    std::nth_element(lvl.begin(), lvl.begin() + static_cast<long>(lvl.size() / 2), lvl.end());
    return lvl[lvl.size() / 2];
}

} // namespace wers
