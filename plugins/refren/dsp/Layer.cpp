#include "Layer.h"

namespace refren {

namespace {

constexpr double kLatencySeconds = 0.030, kLineSeconds = 0.12, kSmoothSeconds = 0.02, kArrivalSeconds = 0.005;
constexpr int kOversamplingLatency = 49, kShortestRead = 2;
constexpr float kOwnLevelDb = 1.5f, kOwnColourDb = 2.0f, kShelfHz = 2500.0f, kLongestTimeMs = 40.0f;
constexpr float kEarliestShare = -0.2f; // of the time range, for voices that may stand before the dry voice
constexpr float kSlowestHz = 0.3f, kFastestHz = 1.2f;

float approach(float value, float target, float coef, float snap) noexcept {
    const float next = value + (target - value) * coef;
    return std::abs(target - next) < snap ? target : next;
}

} // namespace

float Layer::nextRandom(uint32_t& state) const noexcept {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) * (2.0f / 16777216.0f) - 1.0f; // -1 to 1
}

void Layer::prepare(double fs, uint32_t seed) {
    fs_ = fs;
    seed_ = seed;
    latency_ = static_cast<int>(std::lround(kLatencySeconds * fs));
    shortestRead_ = kShortestRead;
    controlStep_ = std::max(1, static_cast<int>(std::lround(32.0 * fs / 48000.0)));
    perSample_ = 1.0f / static_cast<float>(controlStep_);
    secondsPerStep_ = static_cast<float>(controlStep_ / fs);
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, fs / controlStep_));
    arrivalSmooth_ = static_cast<float>(wers::onePoleCoeff(kArrivalSeconds, fs / controlStep_));
    shelfCoef_ = static_cast<float>(wers::onePoleCoeff(1.0 / (2.0 * wers::kPi * kShelfHz), fs));
    int size = 1;
    while (size < static_cast<int>(kLineSeconds * fs) + 8) size *= 2;
    line_.assign(static_cast<size_t>(size), 0.0f);
    lineMask_ = size - 1;
    reset();
}

void Layer::reset() noexcept {
    std::fill(line_.begin(), line_.end(), 0.0f);
    writePos_ = 0;
    written_ = 0;
    active_ = 0;
    first_ = true;
    uint32_t state = seed_ * 2654435761u + 12345u;
    for (auto& v : voice_) {
        v.ownGain = wers::dbToGain(kOwnLevelDb * nextRandom(state));
        v.highGain = wers::dbToGain(kOwnColourDb * nextRandom(state));
        v.rateHz = kSlowestHz + (kFastestHz - kSlowestHz) * (0.5f + 0.5f * nextRandom(state));
        v.random = state ^ 0x5bd1e995u;
        v.phase = 0.5f + 0.5f * nextRandom(state);
        v.from = nextRandom(v.random);
        v.to = nextRandom(v.random);
        v.lowState = 0.0f;
        v.arrival = 0.0f;
        v.delay.set(0.0f);
        v.left.set(0.0f);
        v.right.set(0.0f);
    }
}

float Layer::panShareOf(int index, int count) noexcept {
    // Voices take turns left and right, moving outwards.
    const float distance = count <= 1 ? 0.0f : static_cast<float>((index / 2) * 2 + (count % 2 == 0 ? 1 : (index % 2 == 0 ? 0 : 2))) / static_cast<float>(count - 1);
    return (index % 2 == 0 ? -1.0f : 1.0f) * std::min(distance, 1.0f);
}

void Layer::placeVoices(int count, bool startsAtZero) noexcept {
    // Places in time come in an order of their own, so the earliest voice is not always the leftmost one.
    uint32_t state = seed_ * 40503u + 977u + static_cast<uint32_t>(count);
    std::array<int, kMaxVoices> order{};
    for (int i = 0; i < count; ++i) order[static_cast<size_t>(i)] = i;
    for (int i = count - 1; i > 0; --i) {
        state = state * 1664525u + 1013904223u;
        std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>((state >> 16) % static_cast<uint32_t>(i + 1))]);
    }
    const float earliest = startsAtZero ? 0.0f : kEarliestShare;
    for (int i = 0; i < count; ++i) {
        auto& v = voice_[static_cast<size_t>(i)];
        const int place = order[static_cast<size_t>(i)];
        v.offsetShare = count == 1 ? 1.0f : earliest + (1.0f - earliest) * static_cast<float>(place) / static_cast<float>(count - 1);
        v.panShare = panShareOf(i, count);
    }
}

void Layer::set(const LayerParams& params, float widthScale, float timeScale, int allowedVoices, int streamDelay) noexcept {
    const int count = params.on ? std::clamp(std::min(params.voices, allowedVoices), 0, kMaxVoices) : 0;
    const bool startsAtZero = !(params.source == Source::Whisper || (params.source == Source::Voice && params.semis == 0 && params.formant == 0.0f));
    const bool placesChanged = count != active_ || startsAtZero != startsAtZero_ || first_;
    active_ = count;
    startsAtZero_ = startsAtZero;
    if (placesChanged && count > 0) placeVoices(count, startsAtZero);

    const float wantedLevel = params.levelDb;
    const float wantedWidth = std::min(1.0f, params.width * widthScale);
    const float wantedTime = std::min(kLongestTimeMs, params.timeMs * timeScale);
    if (first_) {
        levelDb_ = wantedLevel;
        width_ = wantedWidth;
        timeMs_ = wantedTime;
        detune_ = params.detuneCents;
    } else {
        levelDb_ = approach(levelDb_, wantedLevel, smooth_, 1.0e-3f);
        width_ = approach(width_, wantedWidth, smooth_, 1.0e-4f);
        timeMs_ = approach(timeMs_, wantedTime, smooth_, 1.0e-3f);
        detune_ = approach(detune_, params.detuneCents, smooth_, 1.0e-3f);
    }
    levelGain_ = count > 0 ? wers::dbToGain(levelDb_) / std::sqrt(static_cast<float>(count)) : 0.0f;

    const float toSamples = static_cast<float>(fs_) * 0.001f;
    const float top = static_cast<float>(latency_ - kOversamplingLatency - streamDelay);
    const float shortest = static_cast<float>(shortestRead_);
    const float pitchShare = std::pow(2.0f, detune_ / 1200.0f) - 1.0f;
    const float halfPi = static_cast<float>(wers::kPi) * 0.5f;
    for (int i = 0; i < count; ++i) {
        auto& v = voice_[static_cast<size_t>(i)];
        // Smooth noise: from one random point to the next, at twice the rate of this voice.
        v.phase += 2.0f * v.rateHz * secondsPerStep_;
        if (v.phase >= 1.0f) {
            v.phase -= 1.0f;
            v.from = v.to;
            v.to = nextRandom(v.random);
        }
        const float blend = 0.5f - 0.5f * std::cos(static_cast<float>(wers::kPi) * v.phase);
        const float noise = v.from + (v.to - v.from) * blend;

        const float swing = static_cast<float>(fs_) * pitchShare / (2.0f * static_cast<float>(wers::kPi) * v.rateHz);
        const float place = top + timeMs_ * toSamples * v.offsetShare;
        const float delay = std::max(place, shortest + swing) + swing * noise;

        const float angle = (0.5f + 0.5f * v.panShare * width_) * halfPi;
        // An emptied line has nothing to read for a while. The voice waits for the first sound to reach it and then
        // comes in softly, because sound that just starts in the middle of a wave is a click.
        if (static_cast<float>(written_) > delay + 3.0f) v.arrival = approach(v.arrival, 1.0f, arrivalSmooth_, 1.0e-4f);
        const float gain = levelGain_ * v.ownGain * v.arrival;
        const float toLeft = gain * std::cos(angle) * 1.41421356f, toRight = gain * std::sin(angle) * 1.41421356f;
        if (first_) {
            v.delay.set(delay);
            v.left.set(toLeft);
            v.right.set(toRight);
        } else {
            v.delay.aim(delay, perSample_);
            v.left.aim(toLeft, perSample_);
            v.right.aim(toRight, perSample_);
        }
    }
    first_ = false;
}

void Layer::tick(float stream, float& left, float& right) noexcept {
    line_[static_cast<size_t>(writePos_)] = stream;
    for (int i = 0; i < active_; ++i) {
        auto& v = voice_[static_cast<size_t>(i)];
        const float delay = v.delay.now;
        v.delay.now += v.delay.step;
        const int whole = static_cast<int>(delay);
        const float t = delay - static_cast<float>(whole);
        const float a = line_[static_cast<size_t>((writePos_ - whole + 1) & lineMask_)], b = line_[static_cast<size_t>((writePos_ - whole) & lineMask_)];
        const float c = line_[static_cast<size_t>((writePos_ - whole - 1) & lineMask_)], d = line_[static_cast<size_t>((writePos_ - whole - 2) & lineMask_)];
        const float read = b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
        v.lowState += (read - v.lowState) * shelfCoef_;
        const float coloured = v.lowState + v.highGain * (read - v.lowState);
        left += v.left.now * coloured;
        right += v.right.now * coloured;
        v.left.now += v.left.step;
        v.right.now += v.right.step;
    }
    writePos_ = (writePos_ + 1) & lineMask_;
    if (written_ <= lineMask_) ++written_;
}

} // namespace refren
