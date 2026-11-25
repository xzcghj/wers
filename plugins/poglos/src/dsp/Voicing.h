#pragma once
// What makes each algorithm itself: its network, its early reflections and its tone. Also the parameters
// of the engine and the decay the controls ask for, which the display draws and the tests measure against.
#include "PogDsp.h"

namespace pog {

enum class Algo : int { Hall = 0, Room, Plate, Chamber, Ambient, Cathedral, Shimmer, Nonlinear, Count };
enum class Era : int { Now = 0, Eighties, Seventies, Count };

inline constexpr int kAlgoCount = static_cast<int>(Algo::Count);
inline constexpr int kEraCount = static_cast<int>(Era::Count);

struct Params {
    Algo algo = Algo::Hall;
    Era era = Era::Now;
    float mix = 0.3f;          // 0..1, equal power
    float predelayMs = 20.0f;  // 0..1000
    float decay = 2.5f;        // seconds of RT60 in the middle of the spectrum; the gate length in Nonlinear
    float size = 0.6f;         // 0..1
    float attack = 0.0f;       // 0..1, how slowly the late part builds; the shape of the gate in Nonlinear
    float balance = 0.5f;      // 0 only early reflections, 1 only the late reverb
    float diffEarly = 0.75f;   // 0..1
    float diffLate = 0.7f;     // 0..1
    float bassMult = 1.2f;     // RT60 below the crossover, as a multiple of decay
    float bassHz = 250.0f;
    float dampHz = 6000.0f;    // above this the decay shortens towards kHighMult times decay
    float highCutHz = 16000.0f;
    float lowCutHz = 40.0f;
    float modRate = 0.6f;      // Hz
    float modDepth = 0.4f;     // 0..1
    float width = 1.0f;        // 0..1.5 of the wet image
    bool freeze = false;
    float duck = 0.0f;         // 0..1
    float shimmer = 0.4f;      // 0..1, Shimmer only
    float shimmerSemis = 12.0f;
};

inline constexpr float kHighMult = 0.25f;   // how much shorter the very top decays, relative to decay
inline constexpr float kMinDecay = 0.1f, kMaxDecay = 30.0f;
inline constexpr float kMaxGate = 2.0f;     // the longest gate of Nonlinear, seconds
inline constexpr float kMaxPredelayMs = 1000.0f;
inline constexpr int kLines = 16;
inline constexpr int kMaxEarlyTaps = 24;
inline constexpr int kGateTaps = 48;

// The size control as a factor on every length of the space: 60% is the space as designed.
inline float sizeScale(float size) noexcept { return std::pow(2.0f, 2.5f * (std::clamp(size, 0.0f, 1.0f) - 0.6f)); }
inline constexpr float kMaxSizeScale = 2.0f;

struct Voice {
    float lineMinMs, lineMaxMs;      // the 16 delay lines at size 60%
    int loopAllpasses;               // diffusers inside every line, 0..2
    float loopApMinMs, loopApMaxMs;
    float loopApMax;                 // their coefficient with late diffusion at 100%
    int inputStages;                 // diffusers before the network, per channel
    float inputMinMs, inputMaxMs;
    float inputMax;                  // their coefficient with early diffusion at 100%
    int earlyTaps;
    float earlyStartMs, earlyEndMs;
    float earlyFall;                 // how fast reflections fade across their span, larger is faster
    float earlyLevel;
    float earlyFeed;                 // how much of the reflections also feeds the late network
    float modMs;                     // modulation depth at 100%
    float buildMs, buildMix;         // a build-up of the late part even with attack at 0
    float wet;                       // level calibration
    float lowShelfDb, lowShelfHz, peakDb, peakHz, peakQ, highShelfDb, highShelfHz;
    uint32_t seed;
};

// Values tuned by ear and by the tests: density, flatness of the tail and level.
inline const Voice& voiceFor(Algo a) noexcept {
    static const Voice voices[kAlgoCount] = {
        // Hall: long lateral reflections, a wide network of medium lines, chorused.
        { 31.0f, 97.0f, 1, 5.1f, 12.7f, 0.62f, 4, 1.3f, 8.9f, 0.72f, 20, 11.0f, 92.0f, 1.6f, 0.72f, 0.25f, 1.15f, 0.0f, 0.0f, 0.70f,
          1.2f, 220.0f, -0.8f, 2600.0f, 0.8f, -1.2f, 9000.0f, 101u },
        // Room: short lines, dense strong reflections, little movement.
        { 12.7f, 47.0f, 2, 2.3f, 7.9f, 0.58f, 4, 0.8f, 4.7f, 0.70f, 24, 2.6f, 36.0f, 1.9f, 1.00f, 0.35f, 0.45f, 0.0f, 0.0f, 0.62f,
          0.0f, 200.0f, 1.0f, 900.0f, 0.9f, -2.0f, 7500.0f, 202u },
        // Plate: no room at all, a burst of diffusion and two allpasses in every line, bright and thin in the low end.
        { 13.7f, 47.0f, 2, 1.9f, 8.3f, 0.70f, 6, 0.45f, 4.9f, 0.76f, 6, 0.7f, 9.0f, 1.2f, 0.45f, 0.40f, 0.85f, 0.0f, 0.0f, 0.62f,
          -2.5f, 260.0f, 0.6f, 2200.0f, 0.7f, 2.0f, 6000.0f, 303u },
        // Chamber: a hard, small concrete room: clustered reflections, a forward middle, almost still.
        { 17.5f, 61.0f, 1, 3.7f, 9.1f, 0.50f, 4, 1.0f, 6.1f, 0.68f, 20, 5.5f, 68.0f, 1.3f, 0.85f, 0.35f, 0.28f, 0.0f, 0.0f, 0.70f,
          -1.0f, 150.0f, 2.0f, 1200.0f, 0.7f, -3.0f, 9000.0f, 404u },
        // Ambient: no reflections to hear, a long soft diffusion, the sound just gets bigger.
        { 21.0f, 71.0f, 2, 4.3f, 10.9f, 0.60f, 6, 2.9f, 13.7f, 0.70f, 10, 8.0f, 60.0f, 1.0f, 0.30f, 0.15f, 0.95f, 30.0f, 0.35f, 0.80f,
          -1.0f, 180.0f, 0.0f, 1000.0f, 0.7f, -0.5f, 9000.0f, 505u },
        // Cathedral: very long lines, sparse distant reflections and a slow build.
        { 53.0f, 191.0f, 2, 7.9f, 21.7f, 0.60f, 5, 2.0f, 12.1f, 0.72f, 16, 24.0f, 220.0f, 0.9f, 0.60f, 0.20f, 1.55f, 170.0f, 0.55f, 1.30f,
          1.5f, 180.0f, -1.0f, 3000.0f, 0.7f, -2.0f, 6000.0f, 606u },
        // Shimmer: a large hall whose tail is pitched up and fed back.
        { 35.0f, 109.0f, 1, 5.3f, 13.1f, 0.62f, 5, 1.5f, 9.7f, 0.72f, 10, 14.0f, 80.0f, 1.6f, 0.40f, 0.15f, 1.35f, 20.0f, 0.2f, 0.85f,
          0.0f, 200.0f, 0.0f, 1000.0f, 0.7f, 0.0f, 8000.0f, 707u },
        // Nonlinear: a dense cloud of taps with its own envelope, cut off at the end of the gate.
        { 11.0f, 37.0f, 1, 2.9f, 6.7f, 0.55f, 5, 1.1f, 9.3f, 0.75f, 12, 4.0f, 30.0f, 1.5f, 0.55f, 0.20f, 0.30f, 0.0f, 0.0f, 1.00f,
          0.5f, 150.0f, 1.0f, 2500.0f, 0.7f, 1.0f, 8000.0f, 808u },
    };
    return voices[std::clamp(static_cast<int>(a), 0, kAlgoCount - 1)];
}

// The lengths of the 16 lines in milliseconds at size 60%: spread evenly on a log scale with a little
// randomness, so no two lines share a period, then dealt out so neighbours in the mixing matrix differ.
inline std::array<float, kLines> lineLengthsMs(Algo a) noexcept {
    const auto& v = voiceFor(a);
    Rng rng(v.seed);
    std::array<float, kLines> sorted{};
    for (int i = 0; i < kLines; ++i) {
        const float t = (static_cast<float>(i) + 0.5f + 0.38f * rng.bipolar()) / static_cast<float>(kLines);
        sorted[static_cast<size_t>(i)] = v.lineMinMs * std::pow(v.lineMaxMs / v.lineMinMs, std::clamp(t, 0.0f, 1.0f));
    }
    static constexpr int deal[kLines] = { 0, 9, 4, 13, 2, 11, 6, 15, 1, 8, 5, 12, 3, 10, 7, 14 };
    std::array<float, kLines> out{};
    for (int i = 0; i < kLines; ++i) out[static_cast<size_t>(i)] = sorted[static_cast<size_t>(deal[i])];
    return out;
}

// Lengths of the allpasses inside line i, in milliseconds (they do not change with size).
inline std::array<float, 2> loopAllpassMs(Algo a, int line) noexcept {
    const auto& v = voiceFor(a);
    Rng rng(v.seed * 31u + static_cast<uint32_t>(line) * 7919u);
    const float t = (static_cast<float>((line * 7) % kLines) + 0.5f + 0.4f * rng.bipolar()) / static_cast<float>(kLines);
    const float first = v.loopApMinMs * std::pow(v.loopApMaxMs / v.loopApMinMs, std::clamp(t, 0.0f, 1.0f));
    return { first, first * (0.61f + 0.2f * rng.unit()) };
}

// Input diffusers for one channel, in milliseconds, longest first so the sound smears before it thickens.
inline std::array<float, 6> inputAllpassMs(Algo a, int channel) noexcept {
    const auto& v = voiceFor(a);
    Rng rng(v.seed * 131u + static_cast<uint32_t>(channel) * 104729u);
    std::array<float, 6> out{};
    const int n = std::max(1, v.inputStages);
    for (int k = 0; k < n; ++k) {
        const float t = (static_cast<float>(n - 1 - k) + 0.5f + 0.3f * rng.bipolar()) / static_cast<float>(n);
        out[static_cast<size_t>(k)] = v.inputMinMs * std::pow(v.inputMaxMs / v.inputMinMs, std::clamp(t, 0.0f, 1.0f));
    }
    return out;
}

struct EarlyTap {
    float ms;          // at size 60%
    float gainL, gainR;
    int source;        // 0 reads the left input, 1 the right
};

// The early reflections of an algorithm. Their energy adds up to earlyLevel squared on each side.
inline int earlyTaps(Algo a, std::array<EarlyTap, kMaxEarlyTaps>& taps) noexcept {
    const auto& v = voiceFor(a);
    const int n = std::min(v.earlyTaps, kMaxEarlyTaps);
    Rng rng(v.seed * 977u + 13u);
    double sumL = 0.0, sumR = 0.0;
    float previous = 0.0f;
    for (int k = 0; k < n; ++k) {
        float t = (static_cast<float>(k) + 0.5f + 0.42f * rng.bipolar()) / static_cast<float>(n);
        // The chamber's walls answer in pairs: a reflection and its near twin.
        if (a == Algo::Chamber && (k & 1)) t = previous + 0.02f;
        t = std::clamp(t, 0.0f, 1.0f);
        previous = t;
        const float ms = v.earlyStartMs + (v.earlyEndMs - v.earlyStartMs) * std::pow(t, 1.15f);
        const float fall = std::exp(-v.earlyFall * t * 2.3f) * (0.65f + 0.35f * rng.unit());
        const float sign = rng.unit() > 0.5f ? 1.0f : -1.0f;
        // Alternate sides, with some of each reflection in the other channel.
        const bool left = ((k + (rng.unit() > 0.8f ? 1 : 0)) & 1) == 0;
        const float angle = (left ? 0.18f : 0.82f) * 0.5f * kPiF + 0.12f * rng.bipolar();
        auto& tap = taps[static_cast<size_t>(k)];
        tap.ms = ms;
        tap.gainL = sign * fall * std::cos(angle);
        tap.gainR = sign * fall * std::sin(angle);
        tap.source = left ? 0 : 1;
        sumL += static_cast<double>(tap.gainL) * tap.gainL;
        sumR += static_cast<double>(tap.gainR) * tap.gainR;
    }
    const float kl = v.earlyLevel / static_cast<float>(std::sqrt(std::max(sumL, 1e-9)));
    const float kr = v.earlyLevel / static_cast<float>(std::sqrt(std::max(sumR, 1e-9)));
    for (int k = 0; k < n; ++k) {
        taps[static_cast<size_t>(k)].gainL *= kl;
        taps[static_cast<size_t>(k)].gainR *= kr;
    }
    return n;
}

// The decay one loop of the network applies, as the controls ask: the middle gain and the two shelves.
struct LoopLoss {
    float mid, low, high; // linear gains per pass at the middle, at DC and at the very top
};

inline LoopLoss loopLossFor(float loopSamples, float fs, float decay, float bassMult) noexcept {
    const float rtMid = std::clamp(decay, kMinDecay, kMaxDecay);
    const float rtLow = std::clamp(rtMid * bassMult, 0.05f, 60.0f);
    const float rtHigh = std::max(0.03f, rtMid * kHighMult);
    auto g = [&](float rt) { return std::pow(10.0f, -3.0f * loopSamples / (rt * fs)); };
    return { g(rtMid), g(rtLow), g(rtHigh) };
}

// |H| of the loss filter of a loop of loopSamples at hz: mid * (1 + (low/mid - 1) LP_bass) * (1 + (high/mid - 1) HP_damp).
inline float loopMagnitude(const LoopLoss& loss, float hz, float bassHz, float dampHz, float fs) noexcept {
    const double w = 2.0 * kPi * hz / fs;
    auto onePoleLp = [&](float fc, double& re, double& im) {
        // Bilinear one pole: g (1 + z^-1) / ((1 + g) + (g - 1) z^-1), g = tan(pi fc / fs).
        const double g = std::tan(kPi * std::clamp(static_cast<double>(fc), 1.0, 0.49 * fs) / fs);
        const double nr = g * (1.0 + std::cos(w)), ni = -g * std::sin(w);
        const double dr = (1.0 + g) + (g - 1.0) * std::cos(w), di = -(g - 1.0) * std::sin(w);
        const double den = dr * dr + di * di;
        re = (nr * dr + ni * di) / den;
        im = (ni * dr - nr * di) / den;
    };
    double lr, li, dr, di;
    onePoleLp(bassHz, lr, li);
    onePoleLp(dampHz, dr, di);
    const double kl = loss.low / loss.mid - 1.0, kh = loss.high / loss.mid - 1.0;
    const double ar = 1.0 + kl * lr, ai = kl * li;
    const double br = 1.0 + kh * (1.0 - dr), bi = kh * (-di);
    return loss.mid * static_cast<float>(std::sqrt((ar * ar + ai * ai) * (br * br + bi * bi)));
}

// The mean loop length of the network in samples, with its allpasses: what the display and tests use.
inline float meanLoopSamples(Algo a, float size, float fs) noexcept {
    const auto lens = lineLengthsMs(a);
    const auto& v = voiceFor(a);
    double sum = 0.0;
    for (int i = 0; i < kLines; ++i) {
        double ms = lens[static_cast<size_t>(i)] * sizeScale(size);
        const auto ap = loopAllpassMs(a, i);
        for (int k = 0; k < v.loopAllpasses; ++k) ms += ap[static_cast<size_t>(k)];
        sum += ms;
    }
    return static_cast<float>(sum / kLines * fs / 1000.0);
}

// The gains one line uses: the shelves would also pull the middle a little, so the middle gain is
// corrected to keep RT60 at kMidHz exactly on the decay control, never letting any frequency gain above 1.
inline constexpr float kMidHz = 1000.0f;
struct LoopGains {
    float mid, kLow, kHigh; // y = mid * (1 + kLow LP_bass) * (1 + kHigh HP_damp)
};

inline LoopGains loopGainsFor(float loopSamples, float fs, float decay, float bassMult, float bassHz, float dampHz, float freeze = 0.0f) noexcept {
    auto loss = loopLossFor(loopSamples, fs, decay, bassMult);
    loss.mid += (1.0f - loss.mid) * freeze;
    loss.low += (1.0f - loss.low) * freeze;
    loss.high += (1.0f - loss.high) * freeze;
    // The ends stay where the multipliers put them; the middle gain is solved for in a few rounds.
    float g = loss.mid, kl = 0.0f, kh = 0.0f;
    for (int round = 0; round < 4; ++round) {
        kl = loss.low / g - 1.0f;
        kh = loss.high / g - 1.0f;
        g = loss.mid / loopMagnitude({ 1.0f, 1.0f + kl, 1.0f + kh }, kMidHz, bassHz, dampHz, fs);
    }
    const float ceiling = 1.0f / ((1.0f + std::max(kl, 0.0f)) * (1.0f + std::max(kh, 0.0f)));
    return { std::min(g, ceiling), kl, kh };
}

// The RT60 the controls ask for at a frequency: what the display draws and the tests measure against.
inline float rt60At(const Params& p, float hz, float fs = 48000.0f) noexcept {
    const float loop = meanLoopSamples(p.algo, p.size, fs);
    const auto g = loopGainsFor(loop, fs, p.decay, p.bassMult, p.bassHz, p.dampHz);
    const float mag = loopMagnitude({ g.mid, g.mid * (1.0f + g.kLow), g.mid * (1.0f + g.kHigh) }, hz, p.bassHz, p.dampHz, fs);
    return -60.0f * loop / (fs * gainToDb(std::min(mag, 0.999999f)));
}

inline const char* algoKey(Algo a) noexcept {
    static const char* keys[kAlgoCount] = { "hall", "room", "plate", "chamber", "ambient", "cathedral", "shimmer", "nonlinear" };
    return keys[std::clamp(static_cast<int>(a), 0, kAlgoCount - 1)];
}

} // namespace pog
