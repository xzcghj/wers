#pragma once
#include "Types.h"
#include <atomic>
#include <functional>

namespace wers {

// Builds frames from 16 kHz signals. The beat may be empty: then it counts as silence.
BandFrames computeFrames(const std::vector<float>& vocal, const std::vector<float>& beat,
                         const std::function<bool(float)>& progress = {});

// Sets start and end of voice stretches from frame levels. Used when no recognizer is available.
struct TimeSpan { double t0, t1; };
std::vector<TimeSpan> segmentVoice(const BandFrames& f);

// Moves word edges to nearby energy edges, without crossing the neighbours.
void refineWordEdges(const BandFrames& f, std::vector<TimeSpan>& spans);

// Consonant spans inside a time range, in seconds.
std::vector<std::pair<float, float>> consonantSpans(const BandFrames& f, double t0, double t1);

// Voice level in a frame in dB, from the bands that matter. Also used for the fingerprint.
float frameVoiceLevelDb(const float* v10) noexcept;

// Median of per-word loudest level, a robust "how loud is this voice" reading.
float voiceReferenceDb(const BandFrames& f);

} // namespace wers
