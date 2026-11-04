#pragma once
// "Odsluch Prawdy": device simulation, streaming normalisation preview and loudness metering.
#include "DeviceProfiles.h"
#include "Loudness.h"

namespace wers {

enum class NormMode : int { Off = 0, Normal14 = 1, Loud11 = 2, Quiet19 = 3 };

// Spotify's published rule: negative gain always, positive gain limited by true peak headroom
// except in Loud mode, which uses a limiter instead.
double spotifyGainDb(double integratedLufs, double truePeakDb, NormMode mode) noexcept;

class Monitor {
public:
    void prepare(double fs);
    void reset() noexcept;
    void setDevice(Device d) noexcept;
    void setNormalisation(NormMode m) noexcept { norm_ = m; }
    // When offline is true the simulation is skipped so it can never end up in an export.
    void process(float* const* ch, int numChannels, int numSamples, bool offline) noexcept;
    const LoudnessMeter& meter() const noexcept { return meter_; }
    double currentGainDb() const noexcept { return appliedGainDb_; }
private:
    double fs_ = 48000.0;
    LoudnessMeter meter_;
    std::array<DeviceFilter, 2> filter_{};
    Device device_ = Device::Studio;
    NormMode norm_ = NormMode::Off;
    double appliedGainDb_ = 0.0, gain_ = 1.0, gainCoeff_ = 0.001;
    // Simple peak limiter used by Loud mode: 5 ms attack, 100 ms release, -1 dB ceiling.
    double limEnv_ = 1.0, limAtt_ = 0.01, limRel_ = 0.001;
};

} // namespace wers
