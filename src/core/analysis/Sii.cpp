#include "Sii.h"
#include "dsp/Place.h"

namespace wers {
namespace {

constexpr std::array<float, kSiiBands> kImportance{ 0.0083f, 0.0095f, 0.0150f, 0.0289f, 0.0440f, 0.0578f, 0.0653f, 0.0711f, 0.0818f, 0.0844f, 0.0882f, 0.0898f, 0.0868f, 0.0844f, 0.0771f, 0.0527f, 0.0364f, 0.0185f };
constexpr std::array<float, kSiiBands> kInternalNoise{ 0.6f, -1.7f, -3.9f, -6.1f, -8.2f, -9.7f, -10.8f, -11.9f, -12.5f, -13.5f, -15.4f, -17.7f, -21.2f, -24.2f, -25.9f, -23.6f, -15.8f, -7.1f };
constexpr std::array<float, kSiiBands> kStandardSpeech{ 32.41f, 34.48f, 34.75f, 33.98f, 34.59f, 34.27f, 32.06f, 28.30f, 25.01f, 23.00f, 20.15f, 17.32f, 13.18f, 11.55f, 9.33f, 5.31f, 2.59f, 1.13f };

struct Tables {
    std::array<float, kSiiBands> tenLogF{};
    std::array<std::array<float, kSiiBands>, kSiiBands> spreadLog{}; // log10(0.89 f_i / f_k) * 3.32
    Tables() {
        for (size_t i = 0; i < kSiiBands; ++i) {
            tenLogF[i] = static_cast<float>(10.0 * std::log10(kSiiFreq[i]));
            for (size_t k = 0; k < kSiiBands; ++k) spreadLog[k][i] = static_cast<float>(3.32 * std::log10(0.89 * kSiiFreq[i] / kSiiFreq[k]));
        }
    }
};
const Tables& tables() { static const Tables t; return t; }

inline double fromDb10(double db) noexcept { return std::exp(db * 0.230258509299405); }

} // namespace

double siiFromLevels(const float* E, const float* N, float* audibility) noexcept {
    const Tables& t = tables();
    std::array<float, kSiiBands> B{}, C{};
    for (size_t i = 0; i < kSiiBands; ++i) {
        B[i] = std::max(E[i] - 24.0f, N[i]);
        C[i] = 0.6f * (B[i] + t.tenLogF[i] - 6.353f) - 80.0f;
    }
    double sii = 0.0;
    for (size_t i = 0; i < kSiiBands; ++i) {
        double z;
        if (i == 0) {
            z = B[0];
        } else {
            double sum = fromDb10(N[i]);
            for (size_t k = 0; k < i; ++k) {
                const double e = B[k] + C[k] * t.spreadLog[k][i];
                if (e > -60.0) sum += fromDb10(e);
            }
            z = 10.0 * std::log10(sum);
        }
        const double d = std::max(z, static_cast<double>(kInternalNoise[i]));
        const double l = std::clamp(1.0 - (E[i] - kStandardSpeech[i] - 10.0) / 160.0, 0.0, 1.0);
        const double k = std::clamp((E[i] - d + 15.0) / 30.0, 0.0, 1.0);
        const double a = l * k;
        if (audibility != nullptr) audibility[i] = static_cast<float>(a);
        sii += kImportance[i] * a;
    }
    return sii;
}

float scoreFromSii(double sii) noexcept {
    const double p = std::pow(1.0 - std::pow(10.0, -std::max(0.0, sii) / 0.4), 3.334);
    return static_cast<float>(std::clamp(p * 100.0, 0.0, 100.0));
}

const std::array<std::array<float, kSiiBands>, kCutBands>& cutSpread() {
    static const auto t = [] {
        std::array<std::array<float, kSiiBands>, kCutBands> m{};
        const double g = dbToGainD(-6.0);
        for (size_t j = 0; j < kCutBands; ++j)
            for (size_t k = 0; k < kSiiBands; ++k)
                m[j][k] = static_cast<float>(-20.0 * std::log10(svfBellMagnitude(kSiiFreq[k], kCutCentres[j], kCutQ, g, 48000.0)) / 6.0);
        return m;
    }();
    return t;
}

const std::array<std::array<float, kSiiBands>, kBoostBands>& boostSpread() {
    static const auto t = [] {
        std::array<std::array<float, kSiiBands>, kBoostBands> m{};
        const double g = dbToGainD(3.0);
        for (size_t j = 0; j < kBoostBands; ++j)
            for (size_t k = 0; k < kSiiBands; ++k)
                m[j][k] = static_cast<float>(20.0 * std::log10(svfBellMagnitude(kSiiFreq[k], kBoostCentres[j], kBoostQ, g, 48000.0)) / 3.0);
        return m;
    }();
    return t;
}

namespace {
// How much the loudness of the whole mix moves with the faders. The two parts add up by power,
// which is close enough for a song, where voice and beat are unrelated signals.
float mixLoudnessShift(const BandFrames& f, const MixState& mix) noexcept {
    if (mix.vocalFaderDb == 0.0f && mix.beatFaderDb == 0.0f) return 0.0f;
    const double v = f.lufsVocal > -70.0 ? std::pow(10.0, f.lufsVocal * 0.1) : 0.0;
    const double b = f.lufsBeat > -70.0 ? std::pow(10.0, f.lufsBeat * 0.1) : 0.0;
    const double before = v + b;
    const double after = v * std::pow(10.0, static_cast<double>(mix.vocalFaderDb) * 0.1) + b * std::pow(10.0, static_cast<double>(mix.beatFaderDb) * 0.1);
    if (before <= 0.0 || after <= 0.0) return 0.0f;
    return static_cast<float>(10.0 * std::log10(after / before));
}
} // namespace

WordScore scoreWord(const BandFrames& f, int first, int last, Device d, const ScoreMods& mods, int stride) {
    WordScore out;
    first = std::clamp(first, 0, f.count);
    last = std::clamp(last, 0, f.count);
    if (last <= first) return out;
    const DeviceBands& dev = deviceBands(d);
    const auto& cut = cutSpread();
    const auto& boost = boostSpread();

    // Playback calibration: the track plays at the device's assumed level after loudness matching.
    const float lufs = (f.lufs > -70.0 ? static_cast<float>(f.lufs) : -14.0f) + mixLoudnessShift(f, mods.mix);
    const float cal = dev.playbackSpl - lufs;

    std::array<float, kSiiBands> boostDb{};
    if (mods.vocalBoostDb != nullptr)
        for (size_t j = 0; j < kBoostBands; ++j)
            for (size_t k = 0; k < kSiiBands; ++k) boostDb[k] += mods.vocalBoostDb[j] * boost[j][k];
    bool hasFixCut = false;
    if (mods.beatCutDb != nullptr)
        for (size_t j = 0; j < kCutBands; ++j) {
            if (mods.beatCutDb[j] <= 0.0f) continue;
            hasFixCut = true;
        }

    // Which frames hold voice, and the average voice spectrum for vowels and consonants.
    std::array<double, kSiiBands> sumV{}, sumC{};
    int nV = 0, nC = 0;
    float peak = -400.0f;
    const auto frameLevel = [&](int i) {
        const float* v = f.v18(i);
        float m = -400.0f;
        for (size_t k = 3; k < kSiiBands; ++k) m = std::max(m, v[k]);
        return m;
    };
    for (int i = first; i < last; ++i) peak = std::max(peak, frameLevel(i));
    const float presence = peak - 30.0f;
    bool anyConsonant = false, anyVowel = false;
    for (int i = first; i < last; ++i) {
        if (frameLevel(i) < presence) continue;
        const bool cons = isConsonant(f.cls[static_cast<size_t>(i)]);
        anyConsonant = anyConsonant || cons;
        anyVowel = anyVowel || !cons;
        const float* v = f.v18(i);
        auto& target = cons ? sumC : sumV;
        for (size_t k = 0; k < kSiiBands; ++k) target[k] += fromDb10(v[k]);
        if (cons) ++nC; else ++nV;
    }
    if (nV + nC == 0) return out;

    std::array<float, kSiiBands> Ev{}, Ec{};
    for (size_t k = 0; k < kSiiBands; ++k) {
        const float base = dev.gainDb[k] + cal + mods.mix.vocalFaderDb;
        if (nV > 0) Ev[k] = powerToDb(sumV[k] / nV, -200.0f) + base;
        if (nC > 0) Ec[k] = powerToDb(sumC[k] / nC, -200.0f) + base;
        // A word without consonant frames gets its boost everywhere.
        Ev[k] += nC == 0 ? boostDb[k] : 0.0f;
        Ec[k] += boostDb[k];
    }

    std::array<float, kSiiBands> noise{}, a{}, sumA{};
    double sV = 0.0, sC = 0.0;
    int cV = 0, cC = 0, cA = 0;
    std::array<double, kSiiBands> beatSum{};
    for (int i = first; i < last; i += std::max(1, stride)) {
        if (frameLevel(i) < presence) continue;
        const float* b = f.b18(i);
        const bool cons = isConsonant(f.cls[static_cast<size_t>(i)]);
        // Live Miejsce and the word fix both act on the beat; the deeper of the two wins per band.
        std::array<float, kCutBands> bellCut{};
        if (mods.mix.placeDepth > 0.0f) {
            const float* v10 = f.v10(i);
            const float* b10 = f.b10(i);
            for (int j = 0; j < kCutBands; ++j) bellCut[static_cast<size_t>(j)] = Place::liveCutDb(v10[j] + mods.mix.vocalFaderDb, b10[j] + mods.mix.beatFaderDb, j, mods.mix.placeDepth);
        }
        if (hasFixCut)
            for (size_t j = 0; j < kCutBands; ++j) bellCut[j] = std::max(bellCut[j], mods.beatCutDb[j]);
        std::array<float, kSiiBands> cutDb{};
        for (size_t j = 0; j < kCutBands; ++j) {
            if (bellCut[j] <= 0.0f) continue;
            for (size_t k = 0; k < kSiiBands; ++k) cutDb[k] += bellCut[j] * cut[j][k];
        }
        for (size_t k = 0; k < kSiiBands; ++k) {
            const double beat = fromDb10(b[k] + dev.gainDb[k] + cal + mods.mix.beatFaderDb - cutDb[k]);
            const double amb = fromDb10(dev.noiseDb[k]);
            noise[k] = powerToDb(beat + amb, -200.0f);
            beatSum[k] += beat / (beat + amb);
        }
        const double s = siiFromLevels(cons && nC > 0 ? Ec.data() : Ev.data(), noise.data(), a.data());
        if (cons && nC > 0) { sC += s; ++cC; } else { sV += s; ++cV; }
        for (size_t k = 0; k < kSiiBands; ++k) sumA[k] += a[k];
        ++cA;
    }
    if (cA == 0) return out;
    double sii;
    if (cC > 0 && cV > 0) sii = 0.4 * sV / cV + 0.6 * sC / cC;
    else if (cC > 0) sii = sC / cC;
    else sii = sV / cV;
    out.sii = sii;
    out.score = scoreFromSii(sii);

    // The band that loses the most importance-weighted audibility, within the range that matters.
    float worst = 0.02f;
    for (size_t k = 5; k < kSiiBands; ++k) {
        const float loss = kImportance[k] * (1.0f - sumA[k] / static_cast<float>(cA));
        if (loss > worst) { worst = loss; out.worstBand = static_cast<int>(k); }
    }
    if (out.worstBand >= 0) {
        const double beatShare = beatSum[static_cast<size_t>(out.worstBand)] / cA;
        out.cause = beatShare >= 0.5 ? Cause::Beat : Cause::TooQuiet;
    }
    return out;
}

} // namespace wers
