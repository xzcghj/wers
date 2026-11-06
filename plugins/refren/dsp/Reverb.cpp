#include "Reverb.h"

namespace refren {

namespace {

// At a size of 0.5. Each is scaled by the size, then moved to the nearest prime above.
constexpr std::array<double, 8> kLineSeconds{ 0.0297, 0.0371, 0.0411, 0.0437, 0.0533, 0.0617, 0.0673, 0.0797 };
constexpr std::array<double, 8> kDriftHz{ 0.11, 0.17, 0.23, 0.29, 0.31, 0.37, 0.41, 0.47 };
constexpr std::array<std::array<double, 4>, 2> kDiffuserSeconds{ { { 0.0031, 0.0047, 0.0073, 0.0113 }, { 0.0034, 0.0052, 0.0079, 0.0121 } } };
constexpr double kSmallestScale = 0.35, kLargestScale = 1.65, kDriftSeconds = 0.0005, kLongestPreSeconds = 0.08;
constexpr double kPreGlideSeconds = 0.1, kSmoothSeconds = 0.02, kPieceSeconds = 0.05, kOctaveTopHz = 6000.0;
constexpr float kDiffuserGain = 0.6f, kIntoLines = 0.35f, kOutOfLines = 0.5f, kSnap = 1.0e-5f;

bool isPrime(int n) noexcept {
    if (n < 2) return false;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0) return false;
    return true;
}

int primeFrom(int n) noexcept {
    while (!isPrime(n)) ++n;
    return n;
}

double scaleOf(float size) noexcept { return kSmallestScale + (kLargestScale - kSmallestScale) * static_cast<double>(size); }

// The next power of two, so positions wrap with a mask.
int roundedUp(int n) noexcept {
    int size = 1;
    while (size < n) size *= 2;
    return size;
}

} // namespace

float Reverb::Diffuser::tick(float x) noexcept {
    const float delayed = data[static_cast<size_t>(position)];
    const float into = x - kDiffuserGain * delayed;
    data[static_cast<size_t>(position)] = into;
    if (++position >= static_cast<int>(data.size())) position = 0;
    return delayed + kDiffuserGain * into;
}

void Reverb::prepare(double fs) {
    fs_ = fs;
    swing_ = static_cast<float>(kDriftSeconds * fs);
    for (size_t k = 0; k < line_.size(); ++k) {
        auto& l = line_[k];
        const int size = roundedUp(static_cast<int>(kLineSeconds[k] * kLargestScale * fs) + static_cast<int>(swing_) + 64);
        l.data.assign(static_cast<size_t>(size), 0.0f);
        l.mask = size - 1;
        l.step = 2.0 * wers::kPi * kDriftHz[k] / fs;
    }
    for (size_t c = 0; c < diffuser_.size(); ++c)
        for (size_t k = 0; k < diffuser_[c].size(); ++k)
            diffuser_[c][k].data.assign(static_cast<size_t>(std::max(1, static_cast<int>(kDiffuserSeconds[c][k] * fs))), 0.0f);
    preSize_ = static_cast<int>(std::ceil(kLongestPreSeconds * fs)) + 4;
    for (auto& p : pre_) p.assign(static_cast<size_t>(preSize_), 0.0f);
    preGlide_ = wers::onePoleCoeff(kPreGlideSeconds, fs);
    smooth_ = static_cast<float>(wers::onePoleCoeff(kSmoothSeconds, fs));
    octaveCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * wers::kPi * kOctaveTopHz / fs));
    piece_ = 2 * std::max(2, static_cast<int>(std::lround(0.5 * kPieceSeconds * fs)));
    window_.resize(static_cast<size_t>(piece_));
    for (int n = 0; n < piece_; ++n) {
        const double s = std::sin(wers::kPi * n / piece_);
        window_[static_cast<size_t>(n)] = static_cast<float>(s * s);
    }
    for (auto& o : octave_) o.data.assign(static_cast<size_t>(piece_ + 2), 0.0f);
    size_ = decay_ = tone_ = -1.0f;
    reset();
}

void Reverb::reset() noexcept {
    size_t k = 0;
    for (auto& l : line_) {
        std::fill(l.data.begin(), l.data.end(), 0.0f);
        l.position = 0;
        l.damp = 0.0f;
        // Every line starts its drift somewhere else.
        const double phase = static_cast<double>(k) * 0.7;
        l.sine = std::sin(phase);
        l.cosine = std::cos(phase);
        ++k;
    }
    for (auto& channel : diffuser_)
        for (auto& d : channel) {
            std::fill(d.data.begin(), d.data.end(), 0.0f);
            d.position = 0;
        }
    for (auto& p : pre_) std::fill(p.begin(), p.end(), 0.0f);
    prePosition_ = 0;
    for (auto& o : octave_) {
        std::fill(o.data.begin(), o.data.end(), 0.0f);
        o.position = 0;
        o.low = 0.0f;
    }
    pieceAt_ = 0;
    shimmering_ = false;
    snap_ = true;
}

void Reverb::updateLengths() noexcept {
    const double scale = scaleOf(size_);
    for (size_t k = 0; k < line_.size(); ++k) line_[k].length = primeFrom(static_cast<int>(kLineSeconds[k] * scale * fs_));
}

void Reverb::updateGains() noexcept {
    for (auto& l : line_) l.gain = static_cast<float>(std::pow(10.0, -3.0 * l.length / (fs_ * static_cast<double>(decay_))));
}

void Reverb::updateDamping() noexcept {
    double mean = 0.0;
    for (const auto& l : line_) mean += static_cast<double>(l.length) / kLines;
    // A longer line stands for a longer way through the air, so it loses more of the top.
    for (auto& l : line_) l.dampCoef = static_cast<float>(1.0 - std::exp(-2.0 * wers::kPi * static_cast<double>(tone_) * mean / (static_cast<double>(l.length) * fs_)));
}

void Reverb::set(float size, float decaySeconds, float toneHz, float preMs, float shimmer) noexcept {
    const bool newSize = size != size_;
    if (newSize) {
        size_ = size;
        updateLengths();
    }
    if (newSize || decaySeconds != decay_) {
        decay_ = decaySeconds;
        updateGains();
    }
    if (newSize || toneHz != tone_) {
        tone_ = toneHz;
        updateDamping();
    }
    preWanted_ = std::round(std::clamp(static_cast<double>(preMs) * 0.001, 0.0, kLongestPreSeconds) * fs_);
    shimmerTarget_ = shimmer;
    if (snap_) {
        preNow_ = preWanted_;
        shimmer_ = shimmer;
        snap_ = false;
    }
}

float Reverb::shifted(OctaveUp& shifter, float x) noexcept {
    const int size = static_cast<int>(shifter.data.size());
    shifter.low += (x - shifter.low) * octaveCoef_;
    shifter.data[static_cast<size_t>(shifter.position)] = shifter.low;
    const auto at = [&](int lag) {
        const int place = shifter.position - lag;
        return shifter.data[static_cast<size_t>(place < 0 ? place + size : place)];
    };
    // Each piece reads ever closer to the writing place, and it is silent at the moment it jumps back.
    const int firstLag = piece_ - pieceAt_ + 1;
    const int secondAt = pieceAt_ + piece_ / 2;
    const int secondLag = piece_ - (secondAt >= piece_ ? secondAt - piece_ : secondAt) + 1;
    const float share = window_[static_cast<size_t>(pieceAt_)];
    const float y = share * at(firstLag) + (1.0f - share) * at(secondLag);
    if (++shifter.position >= size) shifter.position = 0;
    return y;
}

void Reverb::tick(float inL, float inR, float& outL, float& outR) noexcept {
    preNow_ += (preWanted_ - preNow_) * preGlide_;
    if (std::abs(preWanted_ - preNow_) < 1.0e-3) preNow_ = preWanted_;
    const auto readPre = [&](const std::vector<float>& line) {
        double place = static_cast<double>(prePosition_) - preNow_;
        if (place < 0.0) place += static_cast<double>(preSize_);
        int first = static_cast<int>(place);
        const auto part = static_cast<float>(place - static_cast<double>(first));
        if (first >= preSize_) first -= preSize_;
        const int second = first + 1 >= preSize_ ? 0 : first + 1;
        return line[static_cast<size_t>(first)] + (line[static_cast<size_t>(second)] - line[static_cast<size_t>(first)]) * part;
    };
    pre_[0][static_cast<size_t>(prePosition_)] = inL;
    pre_[1][static_cast<size_t>(prePosition_)] = inR;
    float intoLeft = readPre(pre_[0]), intoRight = readPre(pre_[1]);
    if (++prePosition_ >= preSize_) prePosition_ = 0;
    for (auto& d : diffuser_[0]) intoLeft = d.tick(intoLeft);
    for (auto& d : diffuser_[1]) intoRight = d.tick(intoRight);
    intoLeft *= kIntoLines;
    intoRight *= kIntoLines;

    std::array<float, kLines> out{};
    float sum = 0.0f;
    for (size_t k = 0; k < line_.size(); ++k) {
        auto& l = line_[k];
        l.sine += l.step * l.cosine;
        l.cosine -= l.step * l.sine;
        const float drift = swing_ * static_cast<float>(l.sine);
        const auto steps = static_cast<int>(std::floor(drift));
        const float part = drift - static_cast<float>(steps);
        const int place = l.position - l.length + steps;
        const float a = l.data[static_cast<size_t>(place & l.mask)], b = l.data[static_cast<size_t>((place + 1) & l.mask)];
        out[k] = a + (b - a) * part;
        sum += out[k];
    }
    const float wideLeft = out[0] - out[2] + out[4] - out[6];
    const float wideRight = out[1] - out[3] + out[5] - out[7];

    // Half the difference with the octave comes off each line of one side and half goes on with the opposite sign,
    // which replaces a part of what these two patterns hold and leaves the rest of the lines alone.
    float moveLeft = 0.0f, moveRight = 0.0f;
    if (shimmer_ > 0.0f || shimmerTarget_ > 0.0f) {
        if (!shimmering_) {
            for (auto& o : octave_) {
                std::fill(o.data.begin(), o.data.end(), 0.0f);
                o.low = 0.0f;
            }
            shimmering_ = true;
        }
        shimmer_ += (shimmerTarget_ - shimmer_) * smooth_;
        if (std::abs(shimmerTarget_ - shimmer_) < kSnap) shimmer_ = shimmerTarget_;
        const float tapLeft = 0.5f * wideLeft, tapRight = 0.5f * wideRight;
        moveLeft = 0.5f * shimmer_ * (tapLeft - shifted(octave_[0], tapLeft));
        moveRight = 0.5f * shimmer_ * (tapRight - shifted(octave_[1], tapRight));
        if (++pieceAt_ >= piece_) pieceAt_ = 0;
    } else {
        shimmering_ = false;
    }

    const float mix = sum * 0.25f; // what a Householder matrix of this size takes from every line
    for (size_t k = 0; k < line_.size(); ++k) {
        auto& l = line_[k];
        const bool rightSide = (k & 1u) != 0u;
        const float sign = (k & 2u) != 0u ? -1.0f : 1.0f;
        const float next = (out[k] - mix - sign * (rightSide ? moveRight : moveLeft)) * l.gain + (rightSide ? intoRight : intoLeft);
        l.damp += (next - l.damp) * l.dampCoef;
        l.data[static_cast<size_t>(l.position)] = l.damp;
        if (++l.position > l.mask) l.position = 0;
    }
    outL = wideLeft * kOutOfLines;
    outR = wideRight * kOutOfLines;
}

} // namespace refren
