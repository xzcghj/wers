#include "Fonem.h"

namespace wers {

void Fonem::prepare(double fs) {
    fs_ = fs;
    lookahead_ = std::max(1, static_cast<int>(std::lround(fs * 0.004)));
    controlEvery_ = std::max(4, static_cast<int>(std::lround(fs / 1500.0)));
    classifier_.prepare(fs);
    const double ctrlRate = fs / controlEvery_;
    envCoef_ = onePoleCoeff(0.010, fs);
    grAtt_ = static_cast<float>(onePoleCoeff(0.005, ctrlRate));
    grRel_ = static_cast<float>(onePoleCoeff(0.080, ctrlRate));
    makeupCoef_ = static_cast<float>(onePoleCoeff(1.0, ctrlRate));
    deEssAtt_ = static_cast<float>(onePoleCoeff(0.0015, ctrlRate));
    deEssRel_ = static_cast<float>(onePoleCoeff(0.025, ctrlRate));
    peakRefCoef_ = onePoleCoeff(2.0, fs);
    peakNowCoef_ = onePoleCoeff(0.020, fs);
    satPowerCoef_ = onePoleCoeff(0.200, fs);
    biasTanh_ = std::tanh(bias_);
    const float sech = 1.0f / std::cosh(bias_);
    biasNorm_ = 1.0f / (sech * sech);
    for (auto& c : chan_) {
        c.look.prepare(lookahead_);
        c.dry.prepare(Halfband2x::kLatency);
        c.popLp.set(120.0, 0.7071, fs);
        c.deEss.set(5000.0, 1.6, fs);
    }
    reset();
}

void Fonem::reset() noexcept {
    classifier_.reset();
    controlCount_ = 0;
    envPower_ = 0.0;
    vowelGrDb_ = makeupDb_ = deEssDb_ = 0.0f;
    centroid_ = 5000.0f;
    gainTarget_ = deEssTarget_ = 1.0;
    popTarget_ = satTarget_ = 0.0;
    gainStep_ = deEssStep_ = popStep_ = satStep_ = 0.0;
    prePush_ = 1.0f;
    peakRef_ = peakNow_ = 0.0;
    peakHold_ = 0;
    satBefore_ = satAfter_ = 0.0;
    satCompDb_ = 0.0f;
    osIdleSamples_ = 0;
    for (auto& c : chan_) {
        c.look.reset(); c.dry.reset(); c.popLp.reset(); c.deEss.reset(); c.os.reset();
        c.gain = c.deEssGain = 1.0;
        c.popAmount = c.satAmount = 0.0;
    }
}

float Fonem::shape(float x) const noexcept {
    // Asymmetric tanh, scaled so the small-signal gain is one and DC stays at zero.
    const float y = std::tanh(x * prePush_ + bias_) - biasTanh_;
    return y * biasNorm_ / prePush_;
}

void Fonem::updateControls() noexcept {
    const Memberships& m = classifier_.current();
    const FonemParams& p = params_;

    // Vowels: gentle compression towards the running reference, only while the sound is voiced.
    const float level = powerToDb(envPower_);
    const float aggr = 0.7f + 0.7f * p.character;
    const float ratio = 1.0f + 2.5f * p.density * aggr;
    const float threshold = m.refDb - 2.0f - 12.0f * p.density;
    const float over = std::max(0.0f, level - threshold);
    const float grTarget = p.density > 0.0f ? -over * (1.0f - 1.0f / ratio) * m.vowel : 0.0f;
    vowelGrDb_ += (grTarget - vowelGrDb_) * (grTarget < vowelGrDb_ ? grAtt_ : grRel_);
    // Makeup follows what the vowels lost, so the voice keeps its level while consonants come forward.
    if (p.density <= 0.0f) { makeupDb_ *= 0.99f; if (makeupDb_ < 1e-3f) makeupDb_ = 0.0f; }
    else if (m.vowel > 0.5f) makeupDb_ += (-vowelGrDb_ - makeupDb_) * makeupCoef_;

    const float breathDb = -18.0f * p.breath * m.breath;
    const float stopDb = 6.0f * p.attack * m.stop;
    if (p.density <= 0.0f) {
        satCompDb_ *= 0.99f;
        if (satCompDb_ < 1e-3f) satCompDb_ = 0.0f;
    } else if (satAfter_ > 1e-12) {
        const float lost = std::clamp(powerToDb(satBefore_) - powerToDb(satAfter_), 0.0f, 6.0f);
        satCompDb_ += (lost - satCompDb_) * makeupCoef_;
    }
    float totalDb = vowelGrDb_ + makeupDb_ + satCompDb_ + breathDb + stopDb;
    if (totalDb > 0.0f) {
        // No lift may take a sound above the recent highest peak.
        const float room = static_cast<float>(gainToDb(static_cast<float>(std::max(peakRef_, 1e-6) / std::max(peakNow_, 1e-6))));
        totalDb = std::min(totalDb, std::max(0.0f, room));
    }
    gainTarget_ = totalDb == 0.0f ? 1.0 : static_cast<double>(dbToGain(totalDb));

    // De-esser follows the centre of the current sibilant, so sz, si and s each get their own band.
    if (p.clarity > 0.0f && m.sib > 0.05f) {
        centroid_ += (std::clamp(m.sibCentroidHz, 3000.0f, static_cast<float>(0.42 * fs_)) - centroid_) * 0.2f;
        for (auto& c : chan_) c.deEss.set(centroid_, 1.6, fs_);
    }
    const float targetRel = -4.0f - 10.0f * p.clarity;
    const float excess = std::max(0.0f, m.highDb - (m.refDb + targetRel));
    const float cut = p.clarity > 0.0f ? std::min(14.0f, excess * (0.4f + 0.5f * p.clarity)) * m.sib : 0.0f;
    deEssDb_ += (cut - deEssDb_) * (cut > deEssDb_ ? deEssAtt_ : deEssRel_);
    if (p.clarity <= 0.0f && deEssDb_ < 1e-3f) deEssDb_ = 0.0f;
    deEssTarget_ = deEssDb_ == 0.0f ? 1.0 : static_cast<double>(dbToGain(-deEssDb_));

    popTarget_ = static_cast<double>(p.clarity * m.stop);

    // Saturation is added only on vowels, so consonants never pick up distortion products.
    // The drive is relative to the recent peak of the voice, so a loud take and a quiet take get the
    // same colour. At the top of the range the peaks lose about 8 dB, in the middle about 3 dB.
    satTarget_ = static_cast<double>(p.density * m.vowel);
    drive_ = (0.4f + 1.4f * p.density) * (0.6f + 0.8f * p.character);
    prePush_ = drive_ / static_cast<float>(std::clamp(peakRef_, 0.01, 1.0));

    const double inv = 1.0 / controlEvery_;
    const auto& c0 = chan_[0];
    gainStep_ = (gainTarget_ - c0.gain) * inv;
    deEssStep_ = (deEssTarget_ - c0.deEssGain) * inv;
    popStep_ = (popTarget_ - c0.popAmount) * inv;
    satStep_ = (satTarget_ - c0.satAmount) * inv;
}

void Fonem::process(float* const* ch, int numChannels, int numSamples) noexcept {
    const int nc = std::clamp(numChannels, 0, kMaxChannels);
    if (nc == 0) return;
    const bool satPossible = params_.density > 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int c = 0; c < nc; ++c) mono += ch[c][i];
        mono /= static_cast<float>(nc);
        classifier_.push(mono);
        envPower_ += (static_cast<double>(mono) * mono - envPower_) * envCoef_;
        const double peak = std::abs(static_cast<double>(mono));
        peakRef_ = peak > peakRef_ ? peak : peakRef_ + (peak - peakRef_) * peakRefCoef_;
        if (peak >= peakNow_) {
            peakNow_ = peak;
            peakHold_ = lookahead_ + Halfband2x::kLatency + 2 * controlEvery_; // until it has left through the output
        } else if (peakHold_ > 0) {
            --peakHold_;
        } else {
            peakNow_ += (peak - peakNow_) * peakNowCoef_;
        }

        if (controlCount_ == 0) updateControls();
        if (++controlCount_ >= controlEvery_) controlCount_ = 0;

        const bool runSat = satPossible || chan_[0].satAmount > 1e-6;
        osIdleSamples_ = runSat ? 0 : osIdleSamples_ + 1;
        for (int c = 0; c < nc; ++c) {
            Channel& s = chan_[static_cast<size_t>(c)];
            s.gain += gainStep_; s.deEssGain += deEssStep_; s.popAmount += popStep_; s.satAmount += satStep_;
            if (controlCount_ == 0) { // the ramp lands exactly on its target, so a neutral state stays neutral
                s.gain = gainTarget_; s.deEssGain = deEssTarget_; s.popAmount = popTarget_; s.satAmount = satTarget_;
            }
            double d = s.look.process(ch[c][i]);
            if (s.popAmount != 0.0) {
                d -= s.popAmount * s.popLp.tick(d).lp;
            } else {
                s.popLp.tick(d);
            }
            const double band = s.deEss.band(d);
            if (s.deEssGain != 1.0) d += (s.deEssGain - 1.0) * band;
            d *= s.gain;
            const float dry = s.dry.process(static_cast<float>(d));
            float out = dry;
            if (runSat || osIdleSamples_ < Halfband2x::kTaps * 2) {
                float a, b;
                s.os.up(static_cast<float>(d), a, b);
                const float sat = static_cast<float>(s.satAmount);
                const float da = (shape(a) - a) * sat;
                const float db = (shape(b) - b) * sat;
                out += s.os.down(da, db);
                if (c == 0 && s.satAmount > 0.3) {
                    satBefore_ += (static_cast<double>(dry) * dry - satBefore_) * satPowerCoef_;
                    satAfter_ += (static_cast<double>(out) * out - satAfter_) * satPowerCoef_;
                }
            }
            ch[c][i] = out;
        }
    }
}

} // namespace wers
