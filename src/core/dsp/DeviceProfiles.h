#pragma once
// Listening devices shared by the monitor (audio) and the word score (model), so both use one curve.
#include "Filters.h"

namespace wers {

inline constexpr int kNumDevices = 5;
enum class Device : int { Phone = 0, Earbuds = 1, Car = 2, Laptop = 3, Studio = 4 };

inline constexpr int kSiiBands = 18;
inline constexpr std::array<double, kSiiBands> kSiiFreq{ 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000 };

struct DeviceSection { Biquad::Type type; double f, q, gainDb; };

struct DeviceProfile {
    bool mono;                   // small single-source playback sums to mono
    double playbackSplAtRef;     // assumed level in dB SPL of programme at the reference loudness
    double noiseAt1k;            // ambient noise spectrum level at 1 kHz, dB SPL per Hz
    double noiseSlopePerOctave;  // tilt of the ambient noise, dB per octave
    int numSections;
    std::array<DeviceSection, 6> sections;
};

const DeviceProfile& deviceProfile(Device d) noexcept;

// Applies a device curve to audio. Allocation free once constructed.
class DeviceFilter {
public:
    void prepare(Device d, double fs) noexcept;
    void reset() noexcept { for (auto& s : sec_) s.reset(); }
    double tick(double x) noexcept { for (int i = 0; i < n_; ++i) x = sec_[static_cast<size_t>(i)].tick(x); return x; }
    double magnitudeDb(double f, double fs) const noexcept;
private:
    std::array<Biquad, 6> sec_{};
    int n_ = 0;
};

// Model inputs for a device: curve gain and ambient noise in each SII band.
struct DeviceBands {
    std::array<float, kSiiBands> gainDb{};
    std::array<float, kSiiBands> noiseDb{};
    float playbackSpl = 75.0f;
};
const DeviceBands& deviceBands(Device d);

} // namespace wers
