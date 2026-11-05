#pragma once
// Shared data types of the word analysis. Plain data, no threads, no JUCE.
#include "dsp/DeviceProfiles.h"
#include <memory>
#include <string>
#include <utility>

namespace wers {

inline constexpr int kAnalysisRate = 16000;
inline constexpr int kFrameHop = 160;            // 10 ms
inline constexpr double kFrameSeconds = 0.01;
inline constexpr float kScoreDies = 50.0f;
inline constexpr float kScoreFades = 65.0f;
inline constexpr float kScoreTarget = 78.0f;

// A frame covers 32 ms starting at index * 10 ms, so its centre is 16 ms later.
inline int frameAtTime(double t) noexcept { return static_cast<int>(std::lround(t / kFrameSeconds - 1.6)); }
inline double frameCentre(int f) noexcept { return f * kFrameSeconds + 0.016; }

enum class SoundClass : uint8_t { Silence = 0, Vowel = 1, Sibilant = 2, Stop = 3, Breath = 4 };
inline bool isConsonant(SoundClass c) noexcept { return c == SoundClass::Sibilant || c == SoundClass::Stop || c == SoundClass::Breath; }

// Why a word is hard to hear on a device.
enum class Cause : uint8_t { None = 0, Beat = 1, TooQuiet = 2 };

struct WordFix {
    bool active = false;
    bool tried = false;        // the planner looked for this target and found no fix that helps
    Device target = Device::Phone;
    std::array<float, kBoostBands> vocalBoostDb{};
    std::array<float, kCutBands> beatCutDb{};
};

struct DeviceVerdict {
    float score = 100.0f;      // current score including the fix when one is active
    float baseScore = 100.0f;  // score without the word fix
    int8_t worstBand = -1;     // SII band losing the most, -1 when nothing stands out
    Cause cause = Cause::None;
};

struct Word {
    std::string text;          // empty when no recognizer ran: shown as a time-labelled fragment
    double t0 = 0.0, t1 = 0.0; // seconds on the project timeline
    float confidence = 1.0f;
    int8_t heardInMix = -1;    // did the speech model also find this word in vocal plus beat: -1 not checked
    std::array<DeviceVerdict, kNumDevices> verdict{};
    WordFix fix;
    std::vector<std::pair<float, float>> consonants; // consonant spans in seconds

    // Weak on this device, with no fix yet and no failed attempt at one.
    bool needsFix(Device d, float threshold) const noexcept {
        return !fix.active && verdict[static_cast<size_t>(d)].score < threshold && !(fix.tried && fix.target == d);
    }
};

// Spectral frames of both signals, the input of every score computation.
struct BandFrames {
    int count = 0;
    double lufs = -14.0;                // the mix as captured
    double lufsVocal = -100.0, lufsBeat = -100.0; // each part alone
    std::vector<float> vocal18, beat18; // PSD in dB re FS^2/Hz, count * kSiiBands
    std::vector<float> vocal10, beat10; // band power in dB, count * kCutBands
    std::vector<SoundClass> cls;        // sound class of each frame

    const float* v18(int f) const noexcept { return vocal18.data() + static_cast<size_t>(f) * kSiiBands; }
    const float* b18(int f) const noexcept { return beat18.data() + static_cast<size_t>(f) * kSiiBands; }
    const float* v10(int f) const noexcept { return vocal10.data() + static_cast<size_t>(f) * kCutBands; }
    const float* b10(int f) const noexcept { return beat10.data() + static_cast<size_t>(f) * kCutBands; }
};

} // namespace wers
