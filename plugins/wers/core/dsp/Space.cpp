#include "Space.h"

namespace wers {

// Measured on real vocals: full amount puts the wet signal about 10 dB below the voice in the gaps.
static constexpr double kWetTrim = 1.6;

void Space::prepare(double fs) {
    fs_ = fs;
    dSize_ = static_cast<int>(fs * 2.2) + 8;
    for (auto& b : dBuf_) b.assign(static_cast<size_t>(dSize_), 0.0f);
    for (auto& b : dBand_) b.set(1400.0, 0.5, fs);
    preLen_ = std::max(2, static_cast<int>(fs * 0.018));
    pre_.assign(static_cast<size_t>(preLen_), 0.0f);
    static constexpr std::array<double, kLines> times{ 0.0297, 0.0371, 0.0411, 0.0437, 0.0533, 0.0617, 0.0673, 0.0797 };
    for (size_t i = 0; i < kLines; ++i) {
        len_[i] = std::max(8, static_cast<int>(times[i] * fs));
        line_[i].assign(static_cast<size_t>(len_[i]), 0.0f);
    }
    updateEvery_ = std::max(4, static_cast<int>(fs / 750.0));
    slew_ = onePoleCoeff(0.25, fs);
    dampCoef_ = 1.0 - std::exp(-2.0 * kPi * 5200.0 / fs);
    envAtt_ = onePoleCoeff(0.005, fs);
    envRel_ = onePoleCoeff(0.040, fs);
    refRel_ = onePoleCoeff(4.0, fs);
    duckAtt_ = onePoleCoeff(0.012, fs);
    duckRel_ = onePoleCoeff(0.150, fs);
    wetCoef_ = onePoleCoeff(0.05, fs);
    reset();
}

void Space::reset() noexcept {
    for (auto& b : dBuf_) std::fill(b.begin(), b.end(), 0.0f);
    for (auto& b : dBand_) b.reset();
    dFbState_.fill(0.0);
    dPos_ = 0;
    std::fill(pre_.begin(), pre_.end(), 0.0f);
    prePos_ = 0;
    for (auto& l : line_) std::fill(l.begin(), l.end(), 0.0f);
    pos_.fill(0); damp_.fill(0.0);
    envPower_ = 0.0; refPower_ = 1e-6; duck_ = 1.0; wetAmount_ = 0.0;
    updateCount_ = 0; lastWidth_ = -1.0f; frozen_ = true;
    dTimeL_ = dTimeR_ = 0.0;
}

void Space::updateReverbGains() noexcept {
    if (params_.width == lastWidth_) return;
    lastWidth_ = params_.width;
    const double rt = 0.5 + 2.3 * static_cast<double>(params_.width);
    for (size_t i = 0; i < kLines; ++i) g_[i] = std::pow(10.0, -3.0 * len_[i] / (fs_ * rt));
}

void Space::process(float* const* ch, int numChannels, int numSamples, double bpm) noexcept {
    const int nc = std::clamp(numChannels, 0, 2);
    if (nc == 0) return;
    if (params_.amount <= 0.0f && wetAmount_ < 1e-6) { frozen_ = true; return; }
    if (frozen_) { // old tail must not come back when the effect is turned on again
        reset();
        frozen_ = false;
    }
    updateReverbGains();
    const double beat = 60.0 / std::clamp(bpm > 1.0 ? bpm : 120.0, 40.0, 300.0);
    const double wantL = std::min(beat * 0.5 * fs_, dSize_ - 4.0);
    const double wantR = std::min(beat * 0.75 * fs_, dSize_ - 4.0);
    if (dTimeL_ <= 0.0) { dTimeL_ = wantL; dTimeR_ = wantR; }
    const double target = static_cast<double>(params_.amount);
    const double delayMix = 0.55 * (1.0 - 0.5 * params_.width);
    const double verbMix = 0.25 + 0.45 * static_cast<double>(params_.width);
    const double feedback = 0.25 + 0.2 * static_cast<double>(params_.width);
    const double spread = 0.3 + 0.7 * static_cast<double>(params_.width);

    for (int i = 0; i < numSamples; ++i) {
        const double in = nc > 1 ? 0.5 * (ch[0][i] + ch[1][i]) : ch[0][i];
        const double p = in * in;
        envPower_ += (p - envPower_) * (p > envPower_ ? envAtt_ : envRel_);
        refPower_ = envPower_ > refPower_ ? envPower_ : refPower_ + (envPower_ - refPower_) * refRel_;
        // 1 under words, 0 in the gaps, relative to the recent level of the voice.
        const double relDb = 10.0 * std::log10((envPower_ + 1e-12) / (refPower_ + 1e-12));
        const double activity = std::clamp((relDb + 24.0) / 12.0, 0.0, 1.0);
        const double duckTarget = 1.0 - 0.88 * activity;
        duck_ += (duckTarget - duck_) * (duckTarget < duck_ ? duckAtt_ : duckRel_);
        wetAmount_ += (target - wetAmount_) * wetCoef_;

        // Delay with a band-limited feedback path.
        dTimeL_ += (wantL - dTimeL_) * slew_;
        dTimeR_ += (wantR - dTimeR_) * slew_;
        const auto readDelay = [this](int c, double t) {
            double rp = dPos_ - t;
            while (rp < 0.0) rp += dSize_;
            int a = static_cast<int>(rp);
            const double fr = rp - a; // before the wrap below, or a rounding case would scale the sample hugely
            if (a >= dSize_) a -= dSize_; // rounding can land exactly on the end
            const int b = a + 1 >= dSize_ ? 0 : a + 1;
            const auto& buf = dBuf_[static_cast<size_t>(c)];
            return buf[static_cast<size_t>(a)] * (1.0 - fr) + buf[static_cast<size_t>(b)] * fr;
        };
        const double dL = readDelay(0, dTimeL_), dR = readDelay(1, dTimeR_);
        dBuf_[0][static_cast<size_t>(dPos_)] = static_cast<float>(in + dBand_[0].band(dR) * feedback);
        dBuf_[1][static_cast<size_t>(dPos_)] = static_cast<float>(in + dBand_[1].band(dL) * feedback);
        if (++dPos_ >= dSize_) dPos_ = 0;

        // Reverb: pre-delay into an 8-line FDN with a Householder matrix.
        const double preOut = pre_[static_cast<size_t>(prePos_)];
        pre_[static_cast<size_t>(prePos_)] = static_cast<float>(in);
        if (++prePos_ >= preLen_) prePos_ = 0;
        std::array<double, kLines> o{};
        double sum = 0.0;
        for (size_t k = 0; k < kLines; ++k) {
            o[k] = line_[k][static_cast<size_t>(pos_[k])];
            sum += o[k];
        }
        const double mix = sum * (2.0 / kLines);
        for (size_t k = 0; k < kLines; ++k) {
            double v = (o[k] - mix) * g_[k] + preOut * 0.5;
            damp_[k] += (v - damp_[k]) * dampCoef_;
            v = damp_[k];
            line_[k][static_cast<size_t>(pos_[k])] = static_cast<float>(v);
            if (++pos_[k] >= len_[k]) pos_[k] = 0;
        }
        const double vL = o[0] - o[2] + o[4] - o[6];
        const double vR = o[1] - o[3] + o[5] - o[7];

        const double wetGain = kWetTrim * wetAmount_ * duck_;
        const double wL = dL * delayMix + vL * verbMix * 0.5;
        const double wR = dR * delayMix + vR * verbMix * 0.5;
        const double wM = 0.5 * (wL + wR), wS = 0.5 * (wL - wR) * spread;
        if (nc > 1) {
            ch[0][i] = static_cast<float>(ch[0][i] + wetGain * (wM + wS));
            ch[1][i] = static_cast<float>(ch[1][i] + wetGain * (wM - wS));
        } else {
            ch[0][i] = static_cast<float>(ch[0][i] + wetGain * wM);
        }
    }
}

} // namespace wers
