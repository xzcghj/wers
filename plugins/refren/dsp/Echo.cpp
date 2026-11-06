#include "Echo.h"

namespace refren {

namespace {

constexpr double kGlideSeconds = 0.25, kSmoothSeconds = 0.02, kNoTempoBpm = 120.0, kSlowestBpm = 30.0, kFastestBpm = 300.0;
constexpr double kSlapSeconds = 0.08, kLongestBeats = 2.0;
constexpr float kMostFeedback = 0.9f;

double beatsOf(EchoTime time) noexcept {
    switch (time) {
        case EchoTime::Sixteenth: return 0.25;
        case EchoTime::Eighth: return 0.5;
        case EchoTime::DottedEighth: return 0.75;
        case EchoTime::Quarter: return 1.0;
        case EchoTime::DottedQuarter: return 1.5;
        case EchoTime::Half: return kLongestBeats;
        case EchoTime::Slap: break;
    }
    return 0.0;
}

} // namespace

void Echo::prepare(double fs) {
    fs_ = fs;
    size_ = static_cast<int>(std::ceil(kLongestBeats * 60.0 / kSlowestBpm * fs)) + 4;
    for (auto& l : line_) l.assign(static_cast<size_t>(size_), 0.0f);
    glide_ = wers::onePoleCoeff(kGlideSeconds, fs);
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, fs));
    toneHz_ = 0.0f;
    reset();
}

void Echo::reset() noexcept {
    for (auto& l : line_) std::fill(l.begin(), l.end(), 0.0f);
    tone_.fill(0.0f);
    position_ = 0;
    snap_ = true;
}

void Echo::set(EchoTime time, float feedback, bool ping, float toneHz, double bpm) noexcept {
    const double tempo = std::clamp(bpm > 0.0 ? bpm : kNoTempoBpm, kSlowestBpm, kFastestBpm);
    const double seconds = time == EchoTime::Slap ? kSlapSeconds : beatsOf(time) * 60.0 / tempo;
    // A whole number of samples, because repeat after repeat a fraction would take away the top.
    timeWanted_ = std::round(seconds * fs_);
    feedbackTarget_ = std::clamp(feedback, 0.0f, kMostFeedback);
    ping_ = ping;
    if (toneHz != toneHz_) {
        toneHz_ = toneHz;
        toneCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * wers::kPi * static_cast<double>(toneHz) / fs_));
    }
    if (snap_) {
        timeNow_ = timeWanted_;
        feedback_ = feedbackTarget_;
        snap_ = false;
    }
}

float Echo::read(const std::vector<float>& line) const noexcept {
    double place = static_cast<double>(position_) - timeNow_;
    if (place < 0.0) place += static_cast<double>(size_);
    int first = static_cast<int>(place);
    const auto part = static_cast<float>(place - static_cast<double>(first));
    if (first >= size_) first -= size_;
    const int second = first + 1 >= size_ ? 0 : first + 1;
    return line[static_cast<size_t>(first)] + (line[static_cast<size_t>(second)] - line[static_cast<size_t>(first)]) * part;
}

void Echo::tick(float inL, float inR, float& outL, float& outR) noexcept {
    timeNow_ += (timeWanted_ - timeNow_) * glide_;
    if (std::abs(timeWanted_ - timeNow_) < 1.0e-3) timeNow_ = timeWanted_;
    feedback_ += (feedbackTarget_ - feedback_) * smooth_;
    outL = read(line_[0]);
    outR = read(line_[1]);
    // Ping-pong starts from the left and every repeat crosses over.
    const float intoLeft = ping_ ? 0.5f * (inL + inR) + feedback_ * outR : inL + feedback_ * outL;
    const float intoRight = ping_ ? feedback_ * outL : inR + feedback_ * outR;
    tone_[0] += (intoLeft - tone_[0]) * toneCoef_;
    tone_[1] += (intoRight - tone_[1]) * toneCoef_;
    line_[0][static_cast<size_t>(position_)] = tone_[0];
    line_[1][static_cast<size_t>(position_)] = tone_[1];
    if (++position_ >= size_) position_ = 0;
}

} // namespace refren
