#include "Psola.h"

namespace refren {

namespace {
constexpr double kLowestHz = 75.0, kSilentStepSeconds = 0.0025, kGateSeconds = 0.005;
constexpr int kHannSize = 1024;
}

void Psola::prepare(double fs, int delaySamples) {
    fs_ = fs;
    delay_ = delaySamples;
    silentStep_ = kSilentStepSeconds * fs;
    longestHalf_ = fs / kLowestHz + 2.0;
    int size = 1;
    while (size < static_cast<int>(2.0 * longestHalf_) + 8) size *= 2;
    overlap_.assign(static_cast<size_t>(size), 0.0f);
    overlapMask_ = size - 1;
    hann_.resize(kHannSize + 2);
    for (int i = 0; i <= kHannSize + 1; ++i) hann_[static_cast<size_t>(i)] = static_cast<float>(0.5 + 0.5 * std::cos(wers::kPi * i / kHannSize));
    gateStep_ = static_cast<float>(1.0 / (kGateSeconds * fs));
    gateLead_ = static_cast<int>(std::lround(0.5 * kGateSeconds * fs));
    reset();
}

void Psola::reset() noexcept {
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
    nextGrain_ = 0.0;
    spacing_ = 0.0;
    gate_ = 0.0f;
    startOfVoice_ = true;
}

void Psola::set(const PsolaSettings& settings) noexcept {
    ratio_ = std::pow(2.0, static_cast<double>(settings.semitones) / 12.0);
    formant_ = std::pow(2.0, static_cast<double>(settings.formantSemitones) / 12.0);
    fixedNote_ = settings.fixedNote;
    noteSpacing_ = fs_ / std::max(1.0, static_cast<double>(settings.noteHz));
}

void Psola::placeGrain(const VoiceAnalysis& voice, const Mark& mark, double halfLength, double gain, int64_t newest) noexcept {
    const double centre = nextGrain_;
    const auto first = std::max(static_cast<int64_t>(std::ceil(centre - halfLength)), newest);
    const auto last = static_cast<int64_t>(std::floor(centre + halfLength));
    const double toTable = kHannSize / halfLength;
    const double markPosition = static_cast<double>(mark.position) + static_cast<double>(mark.fraction);
    for (int64_t i = first; i <= last; ++i) {
        const double offset = static_cast<double>(i) - centre;
        const double place = std::abs(offset) * toTable;
        const int cell = std::min(static_cast<int>(place), kHannSize);
        const auto part = static_cast<float>(place - cell);
        const float window = hann_[static_cast<size_t>(cell)] + part * (hann_[static_cast<size_t>(cell) + 1] - hann_[static_cast<size_t>(cell)]);
        overlap_[static_cast<size_t>(i & overlapMask_)] += static_cast<float>(gain) * window * voice.sample(markPosition + formant_ * offset);
    }
}

float Psola::tick(const VoiceAnalysis& voice) noexcept {
    const int64_t newest = voice.now() - 1;
    while (nextGrain_ - longestHalf_ <= static_cast<double>(newest)) {
        const int64_t wanted = static_cast<int64_t>(nextGrain_) - delay_;
        PitchReading pitch = voice.pitchAt(wanted);
        // At a low pitch the pieces stand far apart. The one closest to the end of the voice still belongs to it,
        // or the stream would stop up to a whole distance early.
        if (!pitch.voiced) pitch = voice.pitchAt(wanted - static_cast<int64_t>(0.5 * spacing_));
        if (!pitch.voiced) {
            if (nextGrain_ > static_cast<double>(newest)) break;
            nextGrain_ += silentStep_;
            startOfVoice_ = true;
            continue;
        }
        const double period = static_cast<double>(pitch.period);
        const double halfLength = formant_ > 1.0 ? period / formant_ : period;
        if (nextGrain_ - halfLength > static_cast<double>(newest)) break;
        Mark mark;
        if (startOfVoice_) {
            // The first piece stands exactly on the first period that follows. From there on the pieces keep their
            // own distance, so whatever offset the first one has, the whole stream keeps.
            startOfVoice_ = false;
            if (voice.markNear(wanted + static_cast<int64_t>(0.5 * period), newest - static_cast<int64_t>(period), mark) && mark.position >= wanted) {
                nextGrain_ = static_cast<double>(mark.position) + static_cast<double>(mark.fraction) + delay_;
                continue;
            }
        }
        if (!voice.markNear(wanted, newest - static_cast<int64_t>(period), mark)) {
            nextGrain_ += period;
            startOfVoice_ = true;
            continue;
        }
        spacing_ = fixedNote_ ? noteSpacing_ : period / ratio_;
        const double gain = std::sqrt(spacing_ / period) * (formant_ > 1.0 ? std::sqrt(formant_) : 1.0);
        placeGrain(voice, mark, halfLength, gain, newest);
        nextGrain_ += spacing_;
    }
    // The gate opens half its time early, so that the middle of its rise stands where the voice begins, and it
    // stays open for the last piece, which may stand half a distance after the end of the voice.
    const int64_t playing = newest - delay_;
    const bool voiced = voice.pitchAt(playing).voiced || voice.pitchAt(playing + gateLead_).voiced || voice.pitchAt(playing - static_cast<int64_t>(0.5 * spacing_)).voiced;
    gate_ = voiced ? std::min(1.0f, gate_ + gateStep_) : std::max(0.0f, gate_ - gateStep_);
    auto& slot = overlap_[static_cast<size_t>(newest & overlapMask_)];
    const float out = slot * gate_;
    slot = 0.0f;
    return out;
}

} // namespace refren
