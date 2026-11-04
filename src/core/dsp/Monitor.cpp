#include "Monitor.h"

namespace wers {

double spotifyGainDb(double lufs, double tpDb, NormMode mode) noexcept {
    if (mode == NormMode::Off || lufs <= -70.0) return 0.0;
    const double target = mode == NormMode::Normal14 ? -14.0 : (mode == NormMode::Loud11 ? -11.0 : -19.0);
    const double wanted = target - lufs;
    if (wanted <= 0.0 || mode == NormMode::Loud11) return wanted;
    return std::min(wanted, std::max(0.0, -1.0 - tpDb));
}

void Monitor::prepare(double fs) {
    fs_ = fs;
    meter_.prepare(fs);
    gainCoeff_ = onePoleCoeff(0.05, fs);
    limAtt_ = onePoleCoeff(0.005, fs);
    limRel_ = onePoleCoeff(0.1, fs);
    for (auto& f : filter_) f.prepare(device_, fs_);
    reset();
}

void Monitor::reset() noexcept {
    meter_.reset();
    for (auto& f : filter_) f.reset();
    gain_ = 1.0; limEnv_ = 1.0; appliedGainDb_ = 0.0;
}

void Monitor::setDevice(Device d) noexcept {
    if (d == device_) return;
    device_ = d;
    for (auto& f : filter_) f.prepare(d, fs_);
}

void Monitor::process(float* const* ch, int numChannels, int numSamples, bool offline) noexcept {
    if (numChannels <= 0) return;
    meter_.process(ch, numChannels, numSamples);
    if (offline) return;
    const bool simulate = device_ != Device::Studio;
    if (!simulate && norm_ == NormMode::Off && gain_ == 1.0) return;

    appliedGainDb_ = meter_.seconds() >= 3.0 ? spotifyGainDb(meter_.integratedLufs(), meter_.truePeakDb(), norm_) : 0.0;
    const double target = dbToGainD(appliedGainDb_);
    const bool mono = simulate && numChannels >= 2 && deviceProfile(device_).mono;
    const bool limit = norm_ == NormMode::Loud11;
    const double ceiling = dbToGainD(-1.0);
    const int nc = std::min(numChannels, 2);
    for (int i = 0; i < numSamples; ++i) {
        double l = ch[0][i], r = nc > 1 ? ch[1][i] : l;
        if (mono) l = r = 0.5 * (l + r);
        if (simulate) { l = filter_[0].tick(l); r = filter_[1].tick(r); }
        gain_ += (target - gain_) * gainCoeff_;
        l *= gain_; r *= gain_;
        if (limit) {
            const double peak = std::max(std::abs(l), std::abs(r));
            const double want = peak > ceiling ? ceiling / peak : 1.0;
            limEnv_ += (want - limEnv_) * (want < limEnv_ ? limAtt_ : limRel_);
            const double g = std::min(limEnv_, want);
            l *= g; r *= g;
        }
        ch[0][i] = static_cast<float>(l);
        if (nc > 1) ch[1][i] = static_cast<float>(r);
    }
}

} // namespace wers
