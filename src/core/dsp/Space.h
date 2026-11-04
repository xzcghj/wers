#pragma once
// Przestrzen: tempo-synced delay and an FDN reverb, kept out of the way of the words.
#include "Filters.h"

namespace wers {

struct SpaceParams {
    float amount = 0.0f; // 0..1
    float width = 0.5f;  // 0 dry and short .. 1 wide and long
};

class Space {
public:
    void prepare(double fs);
    void reset() noexcept;
    void setParams(const SpaceParams& p) noexcept { params_ = p; }
    void process(float* const* ch, int numChannels, int numSamples, double bpm) noexcept;
    float duckDb() const noexcept { return gainToDb(duck_); }
private:
    static constexpr int kLines = 8;
    void updateReverbGains() noexcept;
    double fs_ = 48000.0;
    SpaceParams params_;
    int updateCount_ = 0, updateEvery_ = 64; // reserved for control-rate updates

    // Delay
    std::array<std::vector<float>, 2> dBuf_{};
    int dPos_ = 0, dSize_ = 1;
    double dTimeL_ = 1000, dTimeR_ = 1000;
    double slew_ = 0.001;
    std::array<double, 2> dFbState_{};
    std::array<Svf, 2> dBand_{};

    // Reverb
    std::vector<float> pre_;
    int prePos_ = 0, preLen_ = 1;
    std::array<std::vector<float>, kLines> line_{};
    std::array<int, kLines> pos_{}, len_{};
    std::array<double, kLines> damp_{}, g_{};
    double dampCoef_ = 0.3;
    float lastWidth_ = -1.0f;
    bool frozen_ = true;

    // Ducking
    double envPower_ = 0.0, envAtt_ = 0.01, envRel_ = 0.001, refPower_ = 1e-6, refRel_ = 1e-5;
    double duck_ = 1.0, duckAtt_ = 0.01, duckRel_ = 0.001;
    double wetAmount_ = 0.0, wetCoef_ = 0.001;
};

} // namespace wers
