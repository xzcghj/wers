#include "DeviceProfiles.h"

namespace wers {

const DeviceProfile& deviceProfile(Device d) noexcept {
    using T = Biquad::Type;
    // Curves and listening conditions are engineering assumptions, documented as such in the spec.
    static const std::array<DeviceProfile, kNumDevices> table{ {
        // Phone speaker: nothing below ~300 Hz, presence peak.
        { true, 68.0, 8.0, -5.0, 5, { { { T::HighPass, 320, 0.9, 0 }, { T::HighPass, 320, 0.9, 0 }, { T::Peak, 900, 1.0, 2 }, { T::Peak, 2900, 1.2, 5 }, { T::LowPass, 13000, 0.7, 0 }, {} } } },
        // Earbuds outdoors: V-shaped curve, ambient noise partly blocked.
        { false, 76.0, 6.0, -6.0, 4, { { { T::HighPass, 30, 0.7, 0 }, { T::LowShelf, 110, 0.7, 3 }, { T::Peak, 3000, 1.5, 3 }, { T::LowPass, 16000, 0.7, 0 }, {}, {} } } },
        // Car: bass-heavy cabin, soft top end, road noise concentrated in the low end.
        { false, 78.0, 15.0, -9.0, 4, { { { T::LowShelf, 90, 0.7, 6 }, { T::Peak, 300, 1.0, -2 }, { T::Peak, 2500, 1.0, -2 }, { T::HighShelf, 7000, 0.7, -5 }, {}, {} } } },
        // Laptop speakers: thin, slightly honky.
        { true, 66.0, 3.0, -4.0, 5, { { { T::HighPass, 220, 0.9, 0 }, { T::HighPass, 220, 0.9, 0 }, { T::Peak, 1500, 1.2, 3 }, { T::Peak, 5000, 1.0, 2 }, { T::LowPass, 15000, 0.7, 0 }, {} } } },
        // Studio monitors: flat, quiet room.
        { false, 82.0, -10.0, -4.0, 0, { {} } },
    } };
    return table[static_cast<size_t>(d)];
}

void DeviceFilter::prepare(Device d, double fs) noexcept {
    const auto& p = deviceProfile(d);
    n_ = p.numSections;
    for (int i = 0; i < n_; ++i) {
        const auto& s = p.sections[static_cast<size_t>(i)];
        sec_[static_cast<size_t>(i)].set(s.type, s.f, s.q, s.gainDb, fs);
    }
    reset();
}

double DeviceFilter::magnitudeDb(double f, double fs) const noexcept {
    double db = 0.0;
    for (int i = 0; i < n_; ++i) db += 20.0 * std::log10(std::max(1e-9, sec_[static_cast<size_t>(i)].magnitude(f, fs)));
    return db;
}

const DeviceBands& deviceBands(Device d) {
    static const std::array<DeviceBands, kNumDevices> table = [] {
        std::array<DeviceBands, kNumDevices> t{};
        const double fs = 96000.0;
        for (int i = 0; i < kNumDevices; ++i) {
            const auto dev = static_cast<Device>(i);
            DeviceFilter f; f.prepare(dev, fs);
            const auto& p = deviceProfile(dev);
            t[static_cast<size_t>(i)].playbackSpl = static_cast<float>(p.playbackSplAtRef);
            for (int b = 0; b < kSiiBands; ++b) {
                const double fc = kSiiFreq[static_cast<size_t>(b)];
                t[static_cast<size_t>(i)].gainDb[static_cast<size_t>(b)] = static_cast<float>(f.magnitudeDb(fc, fs));
                t[static_cast<size_t>(i)].noiseDb[static_cast<size_t>(b)] = static_cast<float>(p.noiseAt1k + p.noiseSlopePerOctave * std::log2(fc / 1000.0));
            }
        }
        return t;
    }();
    return table[static_cast<size_t>(d)];
}

} // namespace wers
