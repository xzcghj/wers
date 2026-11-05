#pragma once
// The sound of Barwa. Saturation is added as a difference on top of the untouched signal, so with no
// difference the sound passes unchanged, and Mask, Tone and Mix only ever change what is added.
#include "CharacterShaper.h"
#include "Crossover.h"
#include "LevelMatch.h"
#include "Oversampler.h"
#include "PhonemeClassifier.h"
#include <atomic>

namespace barwa {

struct BandParams {
    Character character = Character::Lamp;
    float driveDb = 0.0f;
    float mix = 1.0f; // 0 to 1
    bool solo = false;
};

struct BarwaParams {
    int bands = 1;
    float f1 = 200.0f, f2 = 3000.0f;
    std::array<BandParams, 3> band{};
    float mask = 1.0f; // 0 to 1
    float tone = 0.0f; // -1 to 1
    float mix = 1.0f;  // 0 to 1
    float outputDb = 0.0f;
    bool autoLevel = true, delta = false;
};

// Takes the offset of asymmetric curves out of what is added.
class DcBlock {
public:
    void prepare(double fs) noexcept;
    void reset() noexcept { in_ = out_ = 0.0; }
    double tick(double x) noexcept {
        out_ = gain_ * (x - in_) + pole_ * out_;
        in_ = x;
        return out_;
    }
private:
    double pole_ = 0.998, gain_ = 0.999, in_ = 0.0, out_ = 0.0;
};

// Tilts what is added around 1 kHz, up to 6 dB each way. At zero it changes nothing.
class Tilt {
public:
    void prepare(double fs) noexcept;
    void reset() noexcept { state_ = 0.0; }
    void set(float tone) noexcept;
    double tick(double x) noexcept {
        const double y = b0_ * x + state_;
        state_ = b1_ * x - a1_ * y;
        return y;
    }
private:
    double warp_ = 0.07, b0_ = 1.0, b1_ = 0.0, a1_ = 0.0, state_ = 0.0;
};

class BarwaEngine {
public:
    void prepare(double fs, int qualityLog2);
    void reset() noexcept;
    void setParams(const BarwaParams& params) noexcept;
    int latencySamples() const noexcept { return lookSamples_ + oversamplingLatency_; }
    void process(float* const* channels, int numChannels, int numSamples) noexcept;
    // How much of the added colour goes through right now, 0 to 1. Safe to read from any thread.
    float maskNow() const noexcept { return maskNow_.load(std::memory_order_relaxed); }
private:
    // A value that changes in a straight line between two control points.
    struct Ramp {
        float now = 0.0f, end = 0.0f, step = 0.0f;
        void set(float value) noexcept { now = end = value; step = 0.0f; }
        void aim(float next, float perSample) noexcept { now = end; end = next; step = (next - now) * perSample; }
    };
    // Closes before a change that cannot be smoothed, waits for the filters to settle and opens again.
    struct Gate {
        enum class Phase { Open, Closing, Settling, Opening };
        Phase phase = Phase::Open;
        float level = 1.0f;
        int wait = 0;
        bool advance(bool changeWanted, float step, int settleTicks) noexcept;
        void openNow() noexcept { phase = Phase::Open; level = 1.0f; wait = 0; }
    };
    struct BandAudio {
        wers::Oversampler oversampler;
        CharacterShaper shaper;
        wers::DelayLine dry;
    };
    struct ChannelAudio {
        wers::DelayLine look;
        wers::Crossover3 split;
        std::array<BandAudio, 3> band;
        DcBlock dc;
        Tilt tilt;
    };
    struct BandControl {
        Character character = Character::Lamp;
        Gate gate;
        float driveDb = 0.0f, mix = 1.0f, solo = 1.0f;
        Ramp gain, weight, keep;
    };

    void controlTick() noexcept;
    void snapToParams() noexcept;
    void aimMask() noexcept;
    void applyBandCount(int bands) noexcept;

    double fs_ = 48000.0;
    int factor_ = 4, lookSamples_ = 192, oversamplingLatency_ = 57;
    int controlStep_ = 32, controlLeft_ = 0, hop_ = 96, settleTicks_ = 8;
    float perSample_ = 1.0f / 32.0f, perHopSample_ = 1.0f / 96.0f, smooth_ = 0.05f, gateStep_ = 0.125f;
    bool snapPending_ = true, anySolo_ = false;

    BarwaParams params_;
    std::array<ChannelAudio, 2> channel_;
    std::array<BandControl, 3> bandControl_;
    int bands_ = 1;
    Gate bandCountGate_;
    float f1_ = 200.0f, f2_ = 3000.0f, tone_ = 0.0f, toneApplied_ = 0.0f;
    float mask_ = 1.0f, mix_ = 1.0f, outputDb_ = 0.0f, dryShare_ = 1.0f, autoShare_ = 1.0f;
    Ramp mixRamp_, maskRamp_, outputRamp_, dryRamp_, autoRamp_, bandCountRamp_;
    wers::PhonemeClassifier classifier_;
    wers::LevelMatch levelMatch_;
    float matchGain_ = 1.0f;
    std::atomic<float> maskNow_{ 1.0f };
};

} // namespace barwa
